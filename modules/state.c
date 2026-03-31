#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ADTList.h"
#include "ADTVector.h"
#include "k08midi.h"
#include "state.h"


// Οι ολοκληρωμένες πληροφορίες της κατάστασης του παιχνιδιού.
// Ο τύπος State είναι pointer σε αυτό το struct, αλλά το ίδιο το struct
// δεν είναι ορατό στον χρήστη.

struct state {
	struct state_info info;  // Γενικές πληροφορίες για την κατάσταση του παιχνιδιού
	MidiFile midi_file;      // Το .mid αρχείο που διαβάσαμε με τη k08midi_file_read
	List midi_events;        // MIDI events προς αναπαραγωγή
	Vector clips;            // Clip προς ηχογράφηση
	uint recording_index;    // index του τρέχοντος (ή επόμενου) clip
};

// Τμήμα του τραγουδιού στο οποίο υπάρχει marker "rec" και ο χρήστης πρέπει να
// ηχογραφήσει, παίζοντας τις νότες από το αντίστοιχο channel του τραγουδιού. Το
// ηχογραφημένο τμήμα αναπαράγεται σε loop στα σημεία που υπάρχουν οι
// αντίστοιχοι "play" markers.

typedef struct clip {
	String name;		// όνομα
	int channel;		// MIDI κανάλι
	double start;		// χρονικό σημείο έναρξης
	double duration;	// διάρκεια
	List recorded;  	// Ηχογραφημένα MIDI_NOTE events
	List plays;     	// Λίστα με τα σημεία αναπαραγωγής του clip (τύπος ClipPlay)
}* Clip;

// Τμήμα αναπαραγωγής ενός clip, που προκύπτει από τα "play" markers.

typedef struct clip_play {
	double start;		// χρονικό σημείο έναρξης
	double duration;	// διάρκεια
}* ClipPlay;


// Bοηθητικές συναρτήσεις /////////////////////////////////////////////////////////////////////////////////
//
// Δημιουργεί και επιστρέφει ένα σημείο αναπαραγωγής clip
static ClipPlay create_clip_play(double start, double duration) {
	ClipPlay play = malloc(sizeof(*play));
	play->start = start;
	play->duration = duration;
	return play;
}

// Δημιουργεί και επιστρέφει ένα clip
static Clip create_clip(State state, String name, int channel, double start, double duration) {
	Clip clip = malloc(sizeof(*clip));
	clip->name = strdup(name);
	clip->channel = channel;
	clip->start = start;
	clip->duration = duration;
	clip->recorded = list_create(free);
	clip->plays = list_create(free);
	return clip;
}

// Καταστρέφει ένα clip
static void destroy_clip(Pointer value) {
	Clip clip = value;
	free(clip->name);
	list_destroy(clip->recorded);
	list_destroy(clip->plays);
	free(clip);
}

// Βρίσκει και επιστρέφει ένα clip με βάση το όνομά του, NULL αν δεν υπάρχει.
static Clip find_clip_by_name(State state, String clip_name) {
	for (int ci = 0; ci < vector_size(state->clips); ci++) {
		Clip clip = vector_get_at(state->clips, ci);
		if (strcmp(clip->name, clip_name) == 0)
			return clip;
	}
	return NULL;
}

// Αντιγράφει στο state->midi_events όλα τα PROGRAM_CHANGE (αλλαγή οργάνου) και
// CONTROL_CHANGE (έλεγχος volume, modulation, sustain, κλπ), ώστε να είναι
// έτοιμα για αναπαραγωγή μαζί με τις ηχογραφημένες νότες. 
static void append_song_control_events(State state) {
	for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)) {
		MidiEvent event = list_node_value(state->midi_file->events, node);
		if (event->type == MIDI_CONTROL_CHANGE || event->type == MIDI_PROGRAM_CHANGE) {
			MidiEvent clone = malloc(sizeof(*clone));
			*clone = *event; // clone
			list_insert_next(state->midi_events, list_last(state->midi_events), clone);
		}
	}
}

// Δημιουργεί clips και clip_plays με βάση τα markers του αρχείου MIDI
static void create_clips(State state) {
	state->clips = vector_create(0, destroy_clip);
	double measure_duration = state_measure_duration(state);

	for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)) {
		MidiEvent event = list_node_value(state->midi_file->events, node);
		if (event->type != MIDI_MARKER)
			continue;

		int channel = 0;
		int measures = 0;
		char clip_name[51]; // στη sscanf διαβάζουμε το πολύ 50 χαρακτήρες για όνομα

		// rec και play markers
		if (sscanf(event->marker, "rec,%50[^,],%d,%d", clip_name, &channel, &measures) == 3) {
			// "rec,<name>,<channel>,<measures>" marker, ορίζει ένα clip προς ηχογράφηση
			Clip clip = create_clip(state, clip_name, channel, event->time, measures * measure_duration);
			vector_insert_last(state->clips, clip);

		} else if (sscanf(event->marker, "play,%50[^,],%d", clip_name, &measures) == 2) {
			// "play,<name>,<measures>" marker, ορίζει ένα σημείο αναπαραγωγής του clip
			Clip clip = find_clip_by_name(state, clip_name);
			assert(clip);	// το .mid αρχείο πρέπει να έχει φτιαχτεί σωστά, για κάθε play να προηγείται ένα rec
			list_insert_next(clip->plays, list_last(clip->plays), create_clip_play(event->time, measures * measure_duration));
		}
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////

State state_create(String midi_file) {
	State state = malloc(sizeof(*state));
	assert(state != NULL);

	state->info.paused = true;
	state->info.game_over = false;
	state->info.accuracy = 0.0;
	state->info.score = 0.0;
	state->info.time = 0.0;
	state->info.level = 1;

	state->midi_file = NULL;
	state->midi_events = list_create(free);

	state->midi_file = k08midi_file_read(midi_file);
	assert(state->midi_file != NULL);

	create_clips(state);
	assert(vector_size(state->clips) > 0);
	state->recording_index = 0;

	list_destroy(state->midi_events);
	state->midi_events = list_create(free);
	append_song_control_events(state);

	return state;
}

//Επιστρέφει τις βασικές πληροφορίες του παιχνιδιού στην κατάσταση state
StateInfo state_info(State state) {
	return &state->info; //Επιστρέφει τη διεύθηνση μνήμης του πεδίου info 
}

//Επιστρέφει το channel που πρέπει να χρησιμοποιείται για live playing
int state_playing_channel(State state) {
	// Το state->recording_index ξεκινάει από το 0 και δείχνει πάντα είτε στο
	// clip που ηχογραφείται τώρα, είτε (αν δεν υπάρχει ενεργή ηχογράφηση) στο
	// αμέσως επόμενο clip. Αφού ολοκληρωθούν όλες οι ηχογραφήσεις το
	// recording_index συνεχίζει να δείχνει το τελευταίο clip.
	//
	// Το κανάλι στο οποίο παίζει "live" ο χρήστης είναι το κανάλι του clip στο
	// οποίο δείχνει το recording_index.

	Clip c = vector_get_at(state->clips, state->recording_index); //Βρίσκουμε σε ποιο clip βρισκόμαστε τώρα
	return c->channel;
}

//Επιστρέφει τη διάρκεια ενός μέτρου του τραγουδιού (σε δευτερόλεπτα)
//Θεωρούμε ότι η διάρκεια είναι σταθερή σε όλο το τραγούδι
double state_measure_duration(State state) {
	MidiFile midi = state->midi_file;
	return midi->time_signature[0] * (60.0 / midi->tempo) * (4.0 / midi->time_signature[1]);
}

//Επιστρέφει τη συνολική διάρκεια (σε δευτερόλεπτα) του τραγουδιού
double state_total_duration(State state) {
	return state->midi_file->duration;
}

//Επιστρέφει μια λίστα με νότες που εμφανίζονται για να παίξει ο χρήστης
//από την τρέχουσα χρονική στιγμή μέχρι και time_window στο μέλλον
//Η λίστα είναι ταξινομημένη σε αύξουσα σειρά ως προς event->time
List state_displayed_notes(State state, double time_window) {
	List notes = list_create(NULL); //Φτιάχνουμε μια νέα κενή λίστα
	for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)){ //Ελέγχουμε όλα τα events του τραγουδιού
		MidiEvent value = list_node_value(state->midi_file->events, node); 
		//Ελέγχουμε αν το event ικανοποιεί τις 3 προυποθέσεις
		if (value->type == MIDI_NOTE && value->channel == state_playing_channel(state) && value->time >= state->info.time && value->time <= state->info.time + time_window){ 
			list_insert_next(notes, list_last(notes), value); //Βάζουμε τη νότα στο τέλος της νέας λίστας
		}
	}

	return notes;
}

//Επιστρέφει μια λίστα με MIDI events προς αναπαραγωγή από "since"
//δευτερόλεπτα στο παρελθόν μέχρι και την τρέχουσα χρονική στιγμή
//Η λίστα επιστρέφεται ταξινομημένη σε μη-φθίνουσα σειρά ως προς event->time
List state_playback_events(State state, double since) {
	List events = list_create(NULL); //Φτιάχνουμε μια νέα κενή λίστα
	for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)){ //Ελέγχουμε όλο το τραγούδι
		MidiEvent value = list_node_value(state->midi_file->events, node);
		//Ελέγχουμε αν ικανοποιεί και τις 3 προυποθέσεις
		if (!(value->type == MIDI_NOTE && value->channel == state_playing_channel(state)) && value->time >= state->info.time - since && value->time <= state->info.time){
			list_insert_next(events, list_last(events), value); //Βάζουμε το event στη λίστα για να ακουστεί
		}
	}

	return events;
}

//Ενημερώνει την κατάσταση state του παιχνιδιού μετά την πάροδο elapsed_time
//χρόνου, και με βάση την κατάσταση πλήκτρων ks
void state_update(State state, KeyState ks, double elapsed_time) {
	//PAUSE/RESUME

	//Αν πατηθεί space και βρίσκεται σε game over, γίνεται reset το παιχνίδι
	if (ks->space && state->info.game_over){
		state->info.game_over = false;
		state->info.paused = false;
	}
	//Αν πατηθεί space κατά τη διάρκεια του παιχνιδιού, αλλάζουμε την κατάσταση pause
	else if (ks->space && !state->info.game_over){
		if (state->info.paused == true){
			state->info.paused = false;
		}
		else{
			state->info.paused = true;
		}    
	}

	//ΧΡΟΝΟΣ - FRAME STEP

	//Ο χρόνος αυξάνεται αν το παιχνίδι δεν είναι ούτε σε pause ούτε σε game over
	//ή αν είναι σε pause και πατήθει το 'n' 
	if ((!state->info.paused && !state->info.game_over) || (state->info.paused && ks->n)){
		state->info.time += elapsed_time;
	}
	//Αλλιώς βγαίνουμε από τη συνάρτηση
	else{
		return;
	}

    //ΗΧΟΓΡΑΦΗΣΗ

	//Ελέγχουμε αν υπάρχει clip προς ηχογράφηση
	if (state->recording_index < vector_size(state->clips)){
		Clip clip = vector_get_at(state->clips, state->recording_index);
        
		//Διατρέχουμε όλα τα πλήκτρα
		for (int i = 0; i < 128; i++){
			if (ks->changed_keys[i] >= 0){ //Ελέγχουμε τι πατήθηκε/αφέθηκε τώρα
                //Εισάγουμε στη λίστα clip->recorded τα νέα events
				MidiEvent new_event1 = malloc(sizeof(struct midi_event));
				new_event1->time = state->info.time - clip->start; //Τρέχων Χρόνος Παιχνιδιού - Χρόνος Έναρξης Clip
                new_event1->channel = clip->channel;
				new_event1->type = MIDI_NOTE;
				new_event1->key = i;
				new_event1->velocity = ks->changed_keys[i];
				list_insert_next(clip->recorded, list_last(clip->recorded), new_event1);
			}
		}
        
		//ΤΕΛΟΣ ΗΧΟΓΡΑΦΗΣΗΣ

		//Ελέγχουμε αν ο συνολικός χρόνος ξεπέρασε το χρονικό όριο του τρέχων clip
		if (state->info.time >= (clip->start + clip->duration)){
			//Διατρέχουμε όλες τις νότες
			for (int i = 0; i < 128; i++){
				//Ελέγχουμε ποιές νότες είναι πατημένες
				if (ks->active_keys[i] > 0){
					//Εισάγουμε στη λίστα clip->recorded το note off event
					MidiEvent note_off_event = malloc(sizeof(struct midi_event));
					note_off_event->time = clip->duration; //Το χρονικό όριο
					note_off_event->channel = clip->channel;
					note_off_event->type = MIDI_NOTE;
					note_off_event->key = i;
					note_off_event->velocity = 0; //0 : μη πατημένο
					list_insert_next(clip->recorded, list_last(clip->recorded), note_off_event);
				}
			}

			//LOOP

			//Διατρέχουμε όλα τα σημεία που πρέπει να παίξει το clip
            for (ListNode node1 = list_first(clip->plays); node1 != LIST_EOF; node1 = list_next(clip->plays, node1)){
				ClipPlay clip_play = list_node_value(clip->plays, node1);
				double duration_counter = 0;
				//Όσο ο μετρητής δεν έχει γεμίσει τη συνολική διάρκεια του play segment
				while (duration_counter < clip_play->duration){
					//Διατρέχουμε όλες τις ηχογραφημένες νότες
					for (ListNode node2 = list_first(clip->recorded); node2 != LIST_EOF; node2 = list_next(clip->recorded, node2)){
                        //Τις αντιγράφουμε και τις εισάγουμε στη λίστα state->midi_events
						MidiEvent event = list_node_value(clip->recorded, node2);
						MidiEvent new_event2 = malloc(sizeof(struct midi_event));
						new_event2->time = clip_play->start + duration_counter + event->time; //Έναρξη Segment + Χρόνος Προηγούμενων Λουπών + Χρόνος Νότας
						new_event2->channel = event->channel;
						new_event2->type = event->type;
						new_event2->key = event->key;
						new_event2->velocity = event->velocity;
						list_insert_next(state->midi_events, list_last(state->midi_events), new_event2);
					}
					duration_counter += clip->duration; //Προσθέτουμε τον χρόνο μιας ολόκληρης λούπας στον μετρητή
				}
			}
            //Πηγαίνουμε στο επόμενο clip
			state->recording_index++;
		}
	}
	
}

void state_destroy(State state) {
	// Προς υλοποίηση
}
