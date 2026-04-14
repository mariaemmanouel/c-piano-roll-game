#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ADTList.h"
#include "ADTVector.h"
#include "k08midi.h"
#include "state.h"

//Οι ολοκληρωμένες πληροφορίες της κατάστασης του παιχνιδιού
struct state {
    struct state_info info; //Γενικές πληροφορίες για την κατάσταση του παιχνιδιού
    MidiFile midi_file;     //Το .mid αρχείο που διαβάσαμε
    Vector song_events;     //Μόνο τα MIDI_NOTE events του τραγουδιού
    Vector control_events;  //Τα CONTROL_CHANGE και PROGRAM_CHANGE events
    Vector recorded_events; //Ηχογραφημένα events από τον παίκτη
    Vector clips;           //Clip προς ηχογράφηση
    uint recording_index;   //index του τρέχοντος (ή επόμενου) clip
};

//Δημιουργεί και επιστρέφει την αρχική κατάσταση του παιχνιδιού με βάση το τραγούδι midi_file
State state_create (String midi_file){
    State state = malloc(sizeof(*state));
    assert(state != NULL);

    //Αρχικοποίηση βασικών πληροφοριών
    state->info.paused = true;
    state->info.game_over = false;
    state->info.accuracy = 0.0;
    state->info.score = 0.0;
    state->info.time = 0.0;
    state->info.level = 1;

    state->midi_file = NULL;
    //Δημιουργία των Vectors
    state->song_events = vector_create(0, free);
    state->control_events = vector_create(0, free);
    state->recorded_events = vector_create(0, free);
    state->clips = vector_create(0, destroy_clip);

    state->midi_file = k08midi_file_read(midi_file);
    assert(state->midi_file != NULL);

    create_clips(state);
    assert(vector_size(state->clips) > 0);
    state->recording_index = 0;

    //Διατρέχουμε το αρχείο MIDI και μοιράζουμε τα events στους αντίστοιχους Vectors
    for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)){
        MidiEvent value = list_node_value(state->midi_file->events, node);

        //Αν είναι νότα
        if (value->type == MIDI_NOTE){
            MidiEvent copy = malloc(sizeof(*copy));
            *copy = *value;
            vector_insert_last(state->song_events, copy);
        }
        //Αν είναι αλλαγή οργάνου ή έντασης
        else if (value->type == MIDI_CONTROL_CHANGE || value->type == MIDI_PROGRAM_CHANGE){
           MidiEvent copy = malloc(sizeof(*copy));
           *copy = *value;
           vector_insert_last(state->control_events, copy); 
        }
    }

    return state;
}

//Επιστρέφει το index του πρώτου event που έχει χρόνο >= target
int find_first_note (Vector v, double target){
    int mid, low = 0, high = vector_size(v) - 1;
    int best = vector_size(v); //Αν δεν βρεθεί κανένα
    
    while (low <= high){
        mid = (low + high) / 2;
        if (vector_get_at(v, mid)->time >= target){
            best = mid; //Αποθηκεύουμε την πιθανή καλύτερη θέση
            high = mid - 1; //Συνεχίζουμε να ψάχνουμε
        }
        else{
            low = mid + 1;
        }
    }

    return best;
}

//Επιστρέφει μια λίστα με νότες που εμφανίζονται για να παίξει ο χρήστης
//από την τρέχουσα χρονική στιγμή μέχρι και time_window στο μέλλον
List state_displayed_notes (State state, double time_window){
    List notes = list_create(NULL); //Φτιάχνουμε μια νέα κενή λίστα
    //Βρίσκουμε την πρώτη νότα που μας ενδιαφέρει
    int start = find_first_note(state->song_events, state->info.time);
    
    //Ξεκινάμε την αναζήτηση από την νότα που βρήκαμε
    for (int i = start; i < vector_size(state->song_events); i++){
        MidiEvent value = vector_get_at(state->song_events, i);

        //Αν βγούμε έξω από το χρονικό παράθυρο, σταματάμε την αναζήτηση
        if (value->time > state->info.time + time_window){
            break;
        }

        //Ελέγχουμε αν η νότα ανήκει στο κανάλι που παίζει ο παίκτης
        if (value->channel == state_playing_channel(state)){
            list_insert_next(notes, list_last(notes), value); //Βάζουμε τη νότα στο τέλος της νέας λίστας
        }
    }

    return notes;
}

//Επιστρέφει μια λίστα με MIDI events προς αναπαραγωγή από "since"
//δευτερόλεπτα στο παρελθόν μέχρι και την τρέχουσα χρονική στιγμή
List state_playback_events (State state, double since){
    //Βρίσκουμε τα σημεία εκκίνησης και για τους δύο Vectors
    int start1 = find_first_note(state->control_events, state->info.time - since);
    int start2 = find_first_note(state->recorded_events, state->info.time - since);

    List events = list_create(NULL); //Φτιάχνουμε μια νέα κενή λίστα
    int i = start1, j = start2;

    //Διατρέχουμε και τους δύο Vectors ταυτόχρονα
    while (i < vector_size(state->control_events) || j < vector_size(state->recorded_events)){
        MidiEvent event1;
        if (i < vector_size(state->control_events)){
            event1 = vector_get_at(state->control_events, i);
        }
        else{
            event1 = NULL;
        }

        MidiEvent event2;
        if (j < vector_size(state->recorded_events)){
            event2 = vector_get_at(state->recorded_events, j);
        }
        else{
            event2 = NULL;
        }

        //Διαλέγουμε το event με τον παλαιότερο χρόνο
        //Αν οι χρόνοι είναι ίσοι προτιμάμε το control event
        MidiEvent add = NULL;
        if (event1 != NULL && (event2 == NULL || event1->time <= event2->time)){
            add = event1;
            i++;
        }
        else if (event2 != NULL){
            add = event2;
            j++;
        }

        //Αν το επόμενο event είναι στο μέλλον σταματάμε
        if (add->time > state->info.time){
            break;
        }
        else{
            list_insert_next(events, list_last(events), add); //Βάζουμε το event στη λίστα για να ακουστεί
        }
    }

    return events;
}