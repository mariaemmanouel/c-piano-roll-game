//////////////////////////////////////////////////////////////////
//
// Test για το state.h module
//
//////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include "acutest.h"			// Απλή βιβλιοθήκη για unit testing

#include "state.h"


///// Βοηθητικές συναρτήσεις ////////////////////////////////////////
//
// σύγκριση double
static bool double_equal(double a, double b) {
	return fabs(a-b) < 1e-6;
}

// αρχικοποίηση πλήκτρων (κανένα πατημένο)
static struct key_state empty_key_state() {
	struct key_state ks = { .space=false, .n=false };
	memset(ks.active_keys, 0, sizeof(ks.active_keys));
	memset(ks.changed_keys, -1, sizeof(ks.changed_keys));
	return ks;
}
/////////////////////////////////////////////////////////////////////


void test_state_create() {
	State state = state_create("test.mid");
	TEST_ASSERT(state != NULL);

	StateInfo info = state_info(state);
	TEST_ASSERT(info != NULL);
	TEST_ASSERT(info->paused);
	TEST_ASSERT(!info->game_over);
	TEST_ASSERT(double_equal(info->time, 0.0));
	TEST_ASSERT(info->level == 1);

	state_destroy(state);
}

void test_state_basic_functions() {
	State state = state_create("test.mid");
	TEST_ASSERT(state != NULL);

	TEST_ASSERT(state_playing_channel(state) == 9);
	TEST_ASSERT(double_equal(state_measure_duration(state), 1.93548));
	TEST_ASSERT(double_equal(state_total_duration(state), 18.987));

	state_destroy(state);
}

void test_state_update() {
	State state = state_create("test.mid");
	TEST_ASSERT(state != NULL);

	// Παράδειγμα:
	struct key_state ks = empty_key_state();
	ks.space = true;

	state_update(state, &ks, 0.0);
	// TEST_ASSERT(!state_info(state)->paused);

	state_destroy(state);
}

void test_state_lists(){
	State state = state_create("test.mid");
	TEST_ASSERT(state != NULL);
    
	//Έλεγχος 1: state_displayed_notes

	//Καλούμε τη συνάρτηση για ένα μικρό παράθυρο και ένα μεγαλύτερο
    List notes1 = state_displayed_notes(state, 1.5);
	List notes2 = state_displayed_notes(state, 5.0);
    
	//Ελέγχουμε αν το μικρό παράθυρο έχει λιγότερες ή ίσες νότες από το μεγαλύτερο
	TEST_ASSERT(list_size(notes1) <= list_size(notes2));
    
	//Ελευθερώνουμε τη μνήμη
    list_destroy(notes1);
	list_destroy(notes2);
    
    //Έλεγχος 2: state_playback_events

	//Καλούμε τη συνάρτηση για ένα μικρό παρελθόν και ένα μεγαλύτερο
    List events1 = state_playback_events(state, 1.5);
	List events2 = state_playback_events(state, 3.5);

	//Ελέγχουμε αν το μικρό παρελθόν έχει λιγότερους ή ίσους backround ήχους από το μεγαλύτερο
	TEST_ASSERT(list_size(events1) <= list_size(events2));

	//Ελευθερώνουμε τη μνήμη
	list_destroy(events1);
	list_destroy(events2);

	state_destroy(state);
}

// Λίστα με όλα τα tests προς εκτέλεση
TEST_LIST = {
	{ "test_state_create", test_state_create },
	{ "test_state_basic_functions", test_state_basic_functions },
	{ "test_state_update", test_state_update },
	{ "test_state_lists", test_state_lists },

	{ NULL, NULL } // τερματίζουμε τη λίστα με NULL
};
