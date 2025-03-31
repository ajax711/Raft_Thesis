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
#define K 2

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
    Message inbox;              // Single inbox for messages (both explicit and non-deterministic)
} NodeState;

/* ----- STATE ARRAYS ----- */

// Array of node states for each step k
NodeState states[K+1][N];

/* ----- HELPER FUNCTIONS ----- */

// Returns max term across all nodes at step k
int get_max_term(int k) {
    int max_term = 0;
    for (int i = 0; i < N; i++) {
        if (states[k][i].current_term > max_term) {
            max_term = states[k][i].current_term;
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

// Count total votes received by node i at step k
int count_votes(int k, int i) {
    int count = 0;
    for (int j = 0; j < N; j++) {
        if (states[k][i].votes_received[j] > 0) {
            count++;
        }
    }
    return count;
}

/* ----- INITIALIZE STATES ----- */

void init_states() {
    // Initialize all node states at step 0
    
    // Node 0 is the initial leader
    states[0][0].node_id = 0;
    states[0][0].role = ROLE_LEADER;
    states[0][0].current_term = 1;
    states[0][0].voted_for = -1;
    states[0][0].timeout = 0;
    states[0][0].active = 1;
    
    // Initialize votes_received array for node 0
    for (int j = 0; j < N; j++) {
        states[0][0].votes_received[j] = 0;
    }
    
    // Initialize inbox for node 0
    states[0][0].inbox.type = MSG_NONE;
    states[0][0].inbox.term = 0;
    states[0][0].inbox.sender = -1;
    states[0][0].inbox.sender_role = -1;
    
    // All other nodes start as followers
    for (int i = 1; i < N; i++) {
        states[0][i].node_id = i;
        states[0][i].role = ROLE_FOLLOWER;
        states[0][i].current_term = 1;
        states[0][i].voted_for = -1;
        states[0][i].timeout = 0;
        states[0][i].active = 1;
        
        // Initialize votes_received array for node i
        for (int j = 0; j < N; j++) {
            states[0][i].votes_received[j] = 0;
        }
        
        // Initialize inbox for node i
        states[0][i].inbox.type = MSG_NONE;
        states[0][i].inbox.term = 0;
        states[0][i].inbox.sender = -1;
        states[0][i].inbox.sender_role = -1;
    }
}

/* ----- TRANSITION RELATIONS ----- */

// Apply follower transition relation from step k to k+1 for node i
_Bool apply_follower_transition(int k, int i) {
    // Get message from inbox
    Message msg = states[k][i].inbox;
    
    // Case 1.1: Empty inbox and timeout occurs
    if (msg.type == MSG_NONE && nondet_bool()) {
        // Timeout and become candidate
        states[k+1][i].timeout = 1;
        states[k+1][i].current_term = states[k][i].current_term + 1;
        states[k+1][i].voted_for = i;  // Vote for self
        states[k+1][i].role = ROLE_CANDIDATE;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Clear inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        // Broadcast vote requests (modeled by updating inboxes of other nodes)
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[k+1][j].inbox.type = MSG_VOTE_REQUEST;
                states[k+1][j].inbox.term = states[k+1][i].current_term;
                states[k+1][j].inbox.sender = i;
                states[k+1][j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
        
        return 1;
    }
    
    // Case 1.2: Process vote request message
    if (msg.type == MSG_VOTE_REQUEST) {
        states[k+1][i].timeout = 0;
        
        // Vote if term is valid and haven't voted yet
        if (msg.term >= states[k][i].current_term && 
            (states[k][i].voted_for == -1 || states[k][i].voted_for == msg.sender)) {
            if (nondet_bool()) { // Non-deterministic vote (modeling amnesia)
                states[k+1][i].voted_for = msg.sender;
                
                // Send vote grant (might be lost)
                if (nondet_bool()) {
                    states[k+1][msg.sender].inbox.type = MSG_VOTE_GRANT;
                    states[k+1][msg.sender].inbox.term = states[k][i].current_term;
                    states[k+1][msg.sender].inbox.sender = i;
                    states[k+1][msg.sender].inbox.sender_role = ROLE_FOLLOWER;
                }
            }
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 1.3: Process heartbeat with valid term
    if (msg.type == MSG_HEARTBEAT && msg.term >= states[k][i].current_term) {
        states[k+1][i].timeout = 0;
        
        if (msg.term > states[k][i].current_term) {
            states[k+1][i].current_term = msg.term;
            states[k+1][i].voted_for = -1;
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 1.4: Process heartbeat with stale term
    if (msg.type == MSG_HEARTBEAT && msg.term < states[k][i].current_term) {
        states[k+1][i].timeout = 1;
        states[k+1][i].current_term = states[k][i].current_term + 1;
        states[k+1][i].voted_for = i;  // Vote for self
        states[k+1][i].role = ROLE_CANDIDATE;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Broadcast vote requests
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[k+1][j].inbox.type = MSG_VOTE_REQUEST;
                states[k+1][j].inbox.term = states[k+1][i].current_term;
                states[k+1][j].inbox.sender = i;
                states[k+1][j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    return 0;
}

// Apply leader transition relation from step k to k+1 for node i
_Bool apply_leader_transition(int k, int i) {
    // Case 2.1: Normal operation - broadcast heartbeats
    for (int j = 0; j < N; j++) {
        if (j != i && nondet_bool()) { // Message might be lost
            states[k+1][j].inbox.type = MSG_HEARTBEAT;
            states[k+1][j].inbox.term = states[k][i].current_term;
            states[k+1][j].inbox.sender = i;
            states[k+1][j].inbox.sender_role = ROLE_LEADER;
        }
    }
    
    // Get message from inbox
    Message msg = states[k][i].inbox;
    
    // Case 2.2: Received message with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[k][i].current_term) {
        states[k+1][i].current_term = msg.term;
        states[k+1][i].role = ROLE_FOLLOWER;
        states[k+1][i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 2.3: Received vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[k][i].current_term) {
        states[k+1][i].current_term = msg.term;
        states[k+1][i].role = ROLE_FOLLOWER;
        states[k+1][i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // If message was processed, clear inbox
    if (msg.type != MSG_NONE) {
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
    }
    
    return 1;
}

// Apply candidate transition relation from step k to k+1 for node i
_Bool apply_candidate_transition(int k, int i) {
    // Get message from inbox
    Message msg = states[k][i].inbox;
    
    // Case 3.1: Received vote
    if (msg.type == MSG_VOTE_GRANT) {
        // The behavior here depends on whether we're injecting the bug
#if INJECT_DUPLICATE_VOTE_BUG
        // BUG: Allow duplicate votes to be counted without checking
        // Always count the vote regardless of whether we've already counted it
        states[k+1][i].votes_received[msg.sender] = 1;
#else
        // Normal behavior: Only count vote if not already counted
        if (states[k][i].votes_received[msg.sender] == 0) {
            states[k+1][i].votes_received[msg.sender] = 1;
        } else {
            // Don't double-count votes
            states[k+1][i].votes_received[msg.sender] = states[k][i].votes_received[msg.sender];
        }
#endif
        
        // If majority, become leader
        if (count_votes(k+1, i) > N/2) {
            states[k+1][i].role = ROLE_LEADER;
            
            // Broadcast heartbeats
            for (int j = 0; j < N; j++) {
                if (j != i && nondet_bool()) { // Message might be lost
                    states[k+1][j].inbox.type = MSG_HEARTBEAT;
                    states[k+1][j].inbox.term = states[k][i].current_term;
                    states[k+1][j].inbox.sender = i;
                    states[k+1][j].inbox.sender_role = ROLE_LEADER;
                }
            }
        } else {
            // No quorum yet, continue requesting votes
            for (int j = 0; j < N; j++) {
                if (j != i && nondet_bool()) { // Message might be lost
                    states[k+1][j].inbox.type = MSG_VOTE_REQUEST;
                    states[k+1][j].inbox.term = states[k][i].current_term;
                    states[k+1][j].inbox.sender = i;
                    states[k+1][j].inbox.sender_role = ROLE_CANDIDATE;
                }
            }
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 3.2: Received heartbeat with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[k][i].current_term) {
        states[k+1][i].current_term = msg.term;
        states[k+1][i].role = ROLE_FOLLOWER;
        states[k+1][i].voted_for = -1;
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 3.3: Received vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[k][i].current_term) {
        states[k+1][i].current_term = msg.term;
        states[k+1][i].role = ROLE_FOLLOWER;
        
        // Non-deterministic vote with amnesia
        if (nondet_bool()) {
            states[k+1][i].voted_for = msg.sender;
            
            // Send vote grant (might be lost)
            if (nondet_bool()) {
                states[k+1][msg.sender].inbox.type = MSG_VOTE_GRANT;
                states[k+1][msg.sender].inbox.term = states[k+1][i].current_term;
                states[k+1][msg.sender].inbox.sender = i;
                states[k+1][msg.sender].inbox.sender_role = ROLE_FOLLOWER;
            }
        }
        
        // Reset vote array
        for (int j = 0; j < N; j++) {
            states[k+1][i].votes_received[j] = 0;
        }
        
        // Clear the processed inbox
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
        
        return 1;
    }
    
    // Case 3.4: No quorum received yet, continue requesting votes
    if (count_votes(k, i) <= N/2 && nondet_bool()) {
        for (int j = 0; j < N; j++) {
            if (j != i && nondet_bool()) { // Message might be lost
                states[k+1][j].inbox.type = MSG_VOTE_REQUEST;
                states[k+1][j].inbox.term = states[k][i].current_term;
                states[k+1][j].inbox.sender = i;
                states[k+1][j].inbox.sender_role = ROLE_CANDIDATE;
            }
        }
    }
    
    // If message was processed, clear inbox
    if (msg.type != MSG_NONE) {
        states[k+1][i].inbox.type = MSG_NONE;
        states[k+1][i].inbox.term = 0;
        states[k+1][i].inbox.sender = -1;
        states[k+1][i].inbox.sender_role = -1;
    }
    
    return 1;
}

// Generate non-deterministic message for node i at step k
void generate_nondet_message(int k, int i) {
    int max_term = get_max_term(k);
    Message msg;
    
    // Non-deterministically choose if we want to generate a message
    if (!nondet_bool()) {
        // No message
        states[k][i].inbox.type = MSG_NONE;
        states[k][i].inbox.term = 0;
        states[k][i].inbox.sender = -1;
        states[k][i].inbox.sender_role = -1;
        return;
    }
    
    // Generate a random message (we'll check constraints later)
    msg.type = (nondet_int() % 4);  // 0-3 for message types
    
    if (msg.type == MSG_NONE) {
        states[k][i].inbox = msg;
        return;
    }
    
    msg.term = (nondet_int() % max_term) + 1;
    msg.sender = nondet_int() % N;
    msg.sender_role = nondet_int() % 3;
    
    // Check if this message satisfies the constraints
    if (satisfies_constraints(states[k][i].role, msg, states[k][i].current_term, max_term)) {
        states[k][i].inbox = msg;
    } else {
        // If not, set to no message
        states[k][i].inbox.type = MSG_NONE;
        states[k][i].inbox.term = 0;
        states[k][i].inbox.sender = -1;
        states[k][i].inbox.sender_role = -1;
    }
}

/* ----- TRANSITION SYSTEM ----- */

// Apply transition for k to k+1, selecting a node non-deterministically
void apply_transition(int k) {
    // Choose a node to make a transition
    int node_i = nondet_int() % N;
    
    // Copy state by default (for nodes that don't transition)
    for (int i = 0; i < N; i++) {
        states[k+1][i] = states[k][i];
    }
    
    // Skip if node is inactive
    if (!states[k][node_i].active) {
        return;
    }
    
    // Generate non-deterministic message for the chosen node
    generate_nondet_message(k, node_i);
    
    // Apply appropriate transition based on role
    switch (states[k][node_i].role) {
        case ROLE_FOLLOWER:
            apply_follower_transition(k, node_i);
            break;
        case ROLE_CANDIDATE:
            apply_candidate_transition(k, node_i);
            break;
        case ROLE_LEADER:
            apply_leader_transition(k, node_i);
            break;
    }
    
    // Non-deterministically toggle active status
    if (nondet_bool()) {
        states[k+1][node_i].active = !states[k][node_i].active;
    }
}

/* ----- SAFETY PROPERTY ----- */

// Check if the safety property holds at step k
_Bool check_safety_property(int time_step) {
    // For each term, check that there is at most one leader
    int leaders_in_term = 0;
        
    for (int i = 0; i < N; i++) {
        if (states[time_step][i].role == ROLE_LEADER) {
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
    
    // Initialize states at step 0
    init_states();
    
    // Check safety property initially (should be true)
    // Apply transitions for all steps without checking safety
    for (int k = 0; k < K; k++) {
        // Apply transition from k to k+1
        __CPROVER_assume(check_safety_property(k));
        apply_transition(k);
    }
    
    // At the final step, check for a safety violation
    // Using the negation to ask CBMC to find a counterexample
    assert(check_safety_property(K));
    
    return 0;
} 
