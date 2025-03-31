/* 
 * RAFT Protocol as Direct SAT Constraints
 * This file implements the logical formulation from raft_sat_formulation.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ----- TYPE DEFINITIONS ----- */

// Number of nodes
#define N 4

// Number of steps to check
#define K 7

// Node Roles
#define ROLE_FOLLOWER 0
#define ROLE_CANDIDATE 1
#define ROLE_LEADER 2

// Message Types
#define MSG_NONE 0
#define MSG_HEARTBEAT 1
#define MSG_VOTE_REQUEST 2
#define MSG_VOTE_GRANT 3

// Bug control flag - check if defined in compilation
#ifndef INJECT_DUPLICATE_VOTE_BUG
#define INJECT_DUPLICATE_VOTE_BUG 0
#else
#define INJECT_DUPLICATE_VOTE_BUG 1
#endif

// Non-deterministic choice generators
int nondet_int();
_Bool nondet_bool();

// Message structure
typedef struct {
    int type;          // Message type (NONE, HEARTBEAT, etc.)
    int term;          // Term of the message
    int sender;        // Sender node ID
    int sender_role;   // Role of the sender
} Message;

// State variables for each node
typedef struct {
    int node_id;                // Identifier for the node
    int role;                   // Current role (FOLLOWER, CANDIDATE, LEADER)
    int current_term;           // Current term number
    int voted_for;              // Who this node voted for (-1 if none)
    _Bool timeout;              // Whether the node has timed out
    _Bool active;               // Whether the node is active
    int votes_received[N];      // Votes received from each node
    Message inbox;              // Single inbox for messages
} NodeState;

/* ----- STATE ARRAY ----- */

// Array of node states (one per node)
NodeState states[N];

/* ----- HELPER FUNCTIONS ----- */

// Returns max term across all nodes
int get_max_term() {
    int max_term = 0;
    for (int i = 0; i < N; i++) {
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
    }
    return max_term;
}

// Determines if message satisfies constraints for receiver
_Bool satisfies_constraints(int receiver_role, Message msg, int receiver_term, int max_term) {
    // Common constraint: term should not exceed max term
    if (msg.term > max_term) return 0;
    
    switch (receiver_role) {
        case ROLE_FOLLOWER:
            // Follower can receive Heartbeat from Leader
            if (msg.type == MSG_HEARTBEAT && msg.sender_role == ROLE_LEADER)
                return 1;
            // Follower can receive Vote Request from Candidate
            if (msg.type == MSG_VOTE_REQUEST && msg.sender_role == ROLE_CANDIDATE)
                return 1;
            // Follower can receive Vote Grant from Follower with term < current_term
            if (msg.type == MSG_VOTE_GRANT && msg.sender_role == ROLE_FOLLOWER && msg.term < receiver_term)
                return 1;
            break;
            
        case ROLE_CANDIDATE:
            // Candidate can receive Heartbeat from Leader
            if (msg.type == MSG_HEARTBEAT && msg.sender_role == ROLE_LEADER)
                return 1;
            // Candidate can receive Vote Grant from Follower with term <= current_term
            if (msg.type == MSG_VOTE_GRANT && msg.sender_role == ROLE_FOLLOWER && msg.term <= receiver_term)
                return 1;
            // Candidate can receive Vote Request from Candidate
            if (msg.type == MSG_VOTE_REQUEST && msg.sender_role == ROLE_CANDIDATE)
                return 1;
            break;
            
        case ROLE_LEADER:
            // Leader can receive Vote Grant from Follower with term <= current_term
            if (msg.type == MSG_VOTE_GRANT && msg.sender_role == ROLE_FOLLOWER && msg.term <= receiver_term)
                return 1;
            // Leader can receive Vote Request from Candidate
            if (msg.type == MSG_VOTE_REQUEST && msg.sender_role == ROLE_CANDIDATE)
                return 1;
            // Leader can receive Heartbeat from Leader
            if (msg.type == MSG_HEARTBEAT && msg.sender_role == ROLE_LEADER)
                return 1;
            break;
    }
    
    return 0;
}

// Count total votes received by node i
int count_votes(int i) {
    int count = 0;
    for (int j = 0; j < N; j++) {
        if (states[i].votes_received[j] > 0) {
            count++;
        }
    }
    return count;
}

/* ----- INITIALIZE STATES ----- */

void init_states() {
    // Node 0 is the initial leader
    states[0].node_id = 0;
    states[0].role = ROLE_LEADER;
    states[0].current_term = 1;
    states[0].voted_for = -1;
    states[0].timeout = 0;
    states[0].active = 1;
    
    // Initialize votes_received array for node 0
    for (int j = 0; j < N; j++) {
        states[0].votes_received[j] = 0;
    }
    
    // Initialize inbox for node 0
    states[0].inbox.type = MSG_NONE;
    states[0].inbox.term = 0;
    states[0].inbox.sender = -1;
    states[0].inbox.sender_role = -1;
    
    // All other nodes start as followers
    for (int i = 1; i < N; i++) {
        states[i].node_id = i;
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = 1;
        states[i].voted_for = -1;
        states[i].timeout = 0;
        states[i].active = 1;
        
        // Initialize votes_received array for node i
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Initialize inbox for node i
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
}

/* ----- TRANSITION RELATIONS ----- */

// Apply follower transition for node i
void apply_follower_transition(int i) {
    // Get message from inbox
    Message msg = states[i].inbox;
    
    // Case 1.1: Empty inbox and timeout occurs
    if (msg.type == MSG_NONE && nondet_bool()) {
        // Timeout and become candidate
        states[i].timeout = 1;
        states[i].current_term = states[i].current_term + 1;
        states[i].voted_for = i;  // Vote for self
        states[i].role = ROLE_CANDIDATE;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        // Broadcast vote requests (modeled by updating inboxes of other nodes)
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[j].inbox.type = MSG_VOTE_REQUEST;
                states[j].inbox.term = states[i].current_term;
                states[j].inbox.sender = i;
                states[j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
        
        return;
    }
    
    // Case 1.2: Process vote request message
    if (msg.type == MSG_VOTE_REQUEST) {
        states[i].timeout = 0;
        
        // Vote if term is valid and haven't voted yet or voted for this sender
        if (msg.term >= states[i].current_term && 
            (states[i].voted_for == -1 || states[i].voted_for == msg.sender)) {
            if (nondet_bool()) { // Non-deterministic vote (modeling amnesia)
                states[i].voted_for = msg.sender;
                
                // Send vote grant (might be lost)
                if (nondet_bool()) {
                    states[msg.sender].inbox.type = MSG_VOTE_GRANT;
                    states[msg.sender].inbox.term = states[i].current_term;
                    states[msg.sender].inbox.sender = i;
                    states[msg.sender].inbox.sender_role = ROLE_FOLLOWER;
                }
            }
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 1.3: Process heartbeat with valid term
    if (msg.type == MSG_HEARTBEAT && msg.term >= states[i].current_term) {
        states[i].timeout = 0;
        
        if (msg.term > states[i].current_term) {
            states[i].current_term = msg.term;
            states[i].voted_for = -1;
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 1.4: Process heartbeat with stale term
    if (msg.type == MSG_HEARTBEAT && msg.term < states[i].current_term) {
        states[i].timeout = 1;
        states[i].current_term = states[i].current_term + 1;
        states[i].voted_for = i;  // Vote for self
        states[i].role = ROLE_CANDIDATE;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Broadcast vote requests
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[j].inbox.type = MSG_VOTE_REQUEST;
                states[j].inbox.term = states[i].current_term;
                states[j].inbox.sender = i;
                states[j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
}

// Apply leader transition for node i
void apply_leader_transition(int i) {
    // Case 2.1: Normal operation - broadcast heartbeats
    for (int j = 0; j < N; j++) {
        if (j != i && nondet_bool()) { // Message might be lost
            states[j].inbox.type = MSG_HEARTBEAT;
            states[j].inbox.term = states[i].current_term;
            states[j].inbox.sender = i;
            states[j].inbox.sender_role = ROLE_LEADER;
        }
    }
    
    // Get message from inbox
    Message msg = states[i].inbox;
    
    // Case 2.2: Received message with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[i].current_term) {
        states[i].current_term = msg.term;
        states[i].role = ROLE_FOLLOWER;
        states[i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 2.3: Received vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[i].current_term) {
        states[i].current_term = msg.term;
        states[i].role = ROLE_FOLLOWER;
        states[i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // If message was processed, clear inbox
    if (msg.type != MSG_NONE) {
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
}

// Apply candidate transition for node i
void apply_candidate_transition(int i) {
    // Get message from inbox
    Message msg = states[i].inbox;
    
    // Case 3.1: Received vote
    if (msg.type == MSG_VOTE_GRANT) {
        // The behavior here depends on whether we're injecting the bug
#if INJECT_DUPLICATE_VOTE_BUG
        // BUG: Allow duplicate votes to be counted without checking
        // Always count the vote regardless of whether we've already counted it
        states[i].votes_received[msg.sender] = 1;
#else
        // Normal behavior: Only count vote if not already counted
        if (states[i].votes_received[msg.sender] == 0) {
            states[i].votes_received[msg.sender] = 1;
        }
#endif
        
        // If majority, become leader
        if (count_votes(i) > N/2) {
            states[i].role = ROLE_LEADER;
            
            // Broadcast heartbeats
            for (int j = 0; j < N; j++) {
                if (j != i && nondet_bool()) { // Message might be lost
                    states[j].inbox.type = MSG_HEARTBEAT;
                    states[j].inbox.term = states[i].current_term;
                    states[j].inbox.sender = i;
                    states[j].inbox.sender_role = ROLE_LEADER;
                }
            }
        } else {
            // No quorum yet, continue requesting votes
            for (int j = 0; j < N; j++) {
                if (j != i && nondet_bool()) { // Message might be lost
                    states[j].inbox.type = MSG_VOTE_REQUEST;
                    states[j].inbox.term = states[i].current_term;
                    states[j].inbox.sender = i;
                    states[j].inbox.sender_role = ROLE_CANDIDATE;
                }
            }
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.2: Received heartbeat with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[i].current_term) {
        states[i].current_term = msg.term;
        states[i].role = ROLE_FOLLOWER;
        states[i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.3: Received vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[i].current_term) {
        states[i].current_term = msg.term;
        states[i].role = ROLE_FOLLOWER;
        
        // Non-deterministic vote with amnesia
        if (nondet_bool()) {
            states[i].voted_for = msg.sender;
            
            // Send vote grant (might be lost)
            if (nondet_bool()) {
                states[msg.sender].inbox.type = MSG_VOTE_GRANT;
                states[msg.sender].inbox.term = states[i].current_term;
                states[msg.sender].inbox.sender = i;
                states[msg.sender].inbox.sender_role = ROLE_FOLLOWER;
            }
        }
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.4: No quorum received yet, continue requesting votes
    if (count_votes(i) <= N/2 && nondet_bool()) {
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[j].inbox.type = MSG_VOTE_REQUEST;
                states[j].inbox.term = states[i].current_term;
                states[j].inbox.sender = i;
                states[j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
    }
    
    // If message was processed, clear inbox
    if (msg.type != MSG_NONE) {
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
}

// Generate non-deterministic message for node i
void generate_nondet_message(int i) {
    int max_term = get_max_term();
    Message msg;
    
    // Non-deterministically choose if we want to generate a message
    if (!nondet_bool()) {
        // No message
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        return;
    }
    
    // Generate a random message (we'll check constraints later)
    msg.type = (nondet_int() % 4);  // 0-3 for message types
    
    if (msg.type == MSG_NONE) {
        states[i].inbox = msg;
        return;
    }
    
    msg.term = (nondet_int() % max_term) + 1;
    msg.sender = nondet_int() % N;
    msg.sender_role = nondet_int() % 3;
    
    // Check if this message satisfies the constraints
    if (satisfies_constraints(states[i].role, msg, states[i].current_term, max_term)) {
        states[i].inbox = msg;
    } else {
        // If not, set to no message
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
}

/* ----- TRANSITION SYSTEM ----- */

// Apply a single transition step, selecting a node non-deterministically
void apply_transition() {
    // Choose a node to make a transition
    int node_i = nondet_int() % N;
    
    // Skip if node is inactive
    if (!states[node_i].active) {
        return;
    }
    
    // Generate non-deterministic message for the chosen node
    generate_nondet_message(node_i);
    
    // Apply appropriate transition based on role
    switch (states[node_i].role) {
        case ROLE_FOLLOWER:
            apply_follower_transition(node_i);
            break;
        case ROLE_CANDIDATE:
            apply_candidate_transition(node_i);
            break;
        case ROLE_LEADER:
            apply_leader_transition(node_i);
            break;
    }
    
    // Non-deterministically toggle active status
    if (nondet_bool()) {
        states[node_i].active = !states[node_i].active;
    }
}

/* ----- SAFETY PROPERTY ----- */

// Check if the safety property holds (at most one leader)
_Bool check_safety_property() {
    // Count the number of leaders
    int leaders_in_term = 0;
        
    for (int i = 0; i < N; i++) {
        if (states[i].role == ROLE_LEADER) {
            leaders_in_term++;
        }
    }
    
    if (leaders_in_term > 1) {
        return 0; // Property violated
    }
    
    return 1; // Property holds
}

/* ----- MAIN FUNCTION ----- */

int main() {
    // Print configuration info
    printf("Running with N=%d nodes, K=%d steps, Duplicate Vote Bug=%s\n", 
           N, K, INJECT_DUPLICATE_VOTE_BUG ? "ENABLED" : "DISABLED");
    
    // Initialize states
    init_states();
    
    // Apply transitions for all steps without checking safety
    for (int k = 0; k < K; k++) {
        // Assume safety holds before each step
        __CPROVER_assume(check_safety_property());
        apply_transition();
    }
    
    // At the final step, check for a safety violation
    assert(check_safety_property());
    
    return 0;
} 