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

struct state {
    struct state_info info;
    MidiFile midi_file;
    Vector song_events;
    Vector control_events;
    Vector recorded_events;
    Vector clips;
    uint recording_index;
};

State state_create (String midi_file){
    State state = malloc(sizeof(*state));
    assert(state != NULL);

    state->info.paused = true;
    state->info.game_over = false;
    state->info.accuracy = 0.0;
    state->info.score = 0.0;
    state->info.time = 0.0;
    state->info.level = 1;

    state->midi_file = NULL;
    state->song_events = vector_create(0, free);
    state->control_events = vector_create(0, free);
    state->recorded_events = vector_create(0, free);
    state->clips = vector_create(0, destroy_clip);

    state->midi_file = k08midi_file_read(midi_file);
    assert(state->midi_file != NULL);

    create_clips(state);
    assert(vector_size(state->clips) > 0);
    state->recording_index = 0;

    for (ListNode node = list_first(state->midi_file->events); node != LIST_EOF; node = list_next(state->midi_file->events, node)){
        MidiEvent value = list_node_value(state->midi_file->events, node);

        if (value->type == MIDI_NOTE){
            MidiEvent copy = malloc(sizeof(*copy));
            *copy = *value;
            vector_insert_last(state->song_events, copy);
        }

        else if (value->type == MIDI_CONTROL_CHANGE || value->type == MIDI_PROGRAM_CHANGE){
           MidiEvent copy = malloc(sizeof(*copy));
           *copy = *value;
           vector_insert_last(state->control_events, copy); 
        }
    }

    return state;
}

int find_first_note (Vector v, double target){
    int mid, low = 0, high = vector_size(v) - 1;
    int best;
    
    while (low <= high){
        mid = (low + high) / 2;
        if (vector_get_at(v, mid)->time >= target){
            best = mid;
            high = mid - 1;
        }
        else{
            low = mid + 1;
        }
    }

    return best;
}

List state_displayed_notes (State state, double time_window){
    List notes = list_create(NULL);
    int start = find_first_note(state->song_events, state->info.time);
    
    for (int i = start; i < vector_size(state->song_events); i++){
        MidiEvent value = vector_get_at(state->song_events, i);
        if (value->time > state->info.time + time_window){
            break;
        }
        if (value->channel == state_playing_channel(state)){
            list_insert_next(notes, list_last(notes), value);
        }
    }

    return notes;
}

List state_playback_events (State state, double since){
    int start1 = find_first_note(state->control_events, state->info.time - since);
    int start2 = find_first_note(state->recorded_events, state->info.time - since);

    List events = list_create(NULL);
    int i = start1, j = start2;
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

        MidiEvent add = NULL;
        if (event1 != NULL && (event2 == NULL || event1->time <= event2->time)){
            add = event1;
            i++;
        }
        else if (event2 != NULL){
            add = event2;
            j++;
        }

        if (add->time > state->info.time){
            break;
        }
        else{
            list_insert_next(events, list_last(events), add);
        }
    }

    return events;
}