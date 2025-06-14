/* 
 * RAFT Protocol as Direct SAT Constraints
 * Optimized version with minimized loops
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

// Declare nondet functions for CBMC
int nondet_int();
_Bool nondet_bool();

/* ----- TYPE DEFINITIONS ----- */

// Number of nodes
#define N 6

// Number of steps-1 to check
#define K 9

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

// Initialization mode flag
#ifndef USE_FIXED_INIT
#define USE_FIXED_INIT 0
#else 
#define USE_FIXED_INIT 1
#endif

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
    int true_votes;            // Count of unique votes (no duplicates)
    int fake_votes;            // Count of all votes (including duplicates)
    Message inbox;              // Single inbox for messages
} NodeState;

/* ----- GLOBAL VARIABLES ----- */

// Global maximum term
int max_term = 0;
NodeState states[N];

/* ----- HELPER FUNCTIONS ----- */

// Determines if message satisfies constraints for receiver
_Bool satisfies_constraints(int receiver_role, Message msg, int receiver_term) {
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
            if (msg.type == MSG_VOTE_GRANT && msg.sender_role == ROLE_FOLLOWER && msg.term <= receiver_term) {
                // ADDED FROM RAFT_SAT_DIRECT: Check unique vote constraints
                
                // Constraint 1: No two candidates in same term can receive votes from same node
                for (int i = 0; i < N; i++) {
                    if (i != msg.sender && i != receiver_role && 
                        states[i].role == ROLE_CANDIDATE && 
                        states[i].current_term == msg.term && 
                        states[i].votes_received[msg.sender] > 0) {
                        return 0;  // Violation: sender already voted for another candidate
                    }
                }
                
                // Constraint 2: Sender must have voted for this candidate or not voted
                if (states[msg.sender].voted_for != -1 && 
                    states[msg.sender].voted_for != receiver_role) {
                    return 0;  // Violation: sender voted for someone else
                }
                
                return 1;
            }
            // Candidate can receive Vote Request from Candidate
            if (msg.type == MSG_VOTE_REQUEST && msg.sender_role == ROLE_CANDIDATE)
                return 1;
            break;
            
        case ROLE_LEADER:
            // Leader can receive Vote Grant from Follower with term <= current_term
            if (msg.type == MSG_VOTE_GRANT && msg.sender_role == ROLE_FOLLOWER && msg.term <= receiver_term) {
                // ADDED FROM RAFT_SAT_DIRECT: Check unique vote constraints for leader
                
                // Constraint: Sender must have voted for this leader or not voted
                if (states[msg.sender].voted_for != -1 && 
                    states[msg.sender].voted_for != receiver_role) {
                    return 0;  // Violation: sender voted for someone else
                }
                
                return 1;
            }
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
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
        states[i].voted_for = i;  // Vote for self
        states[i].role = ROLE_CANDIDATE;
        
        // Reset vote array and counters using memset
        memset(states[i].votes_received, 0, N * sizeof(int));
        states[i].true_votes = 0;
        states[i].fake_votes = 0;
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        // Broadcast vote requests
        // Removed broadcast logic as we're using abstract message generation
        
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
                
                // // Send vote grant (might be lost)
                // if (nondet_bool()) {
                //     states[msg.sender].inbox.type = MSG_VOTE_GRANT;
                //     states[msg.sender].inbox.term = states[i].current_term;
                //     states[msg.sender].inbox.sender = i;
                //     states[msg.sender].inbox.sender_role = ROLE_FOLLOWER;
                // }
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
        
        // Update term if message term is higher
        if (msg.term > states[i].current_term) {
            states[i].current_term = msg.term;
            if (states[i].current_term > max_term) {
                max_term = states[i].current_term;
            }
            states[i].voted_for = -1;  // Reset vote
        }
        
        // Clear the processed inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
}

// Apply candidate transition for node i
void apply_candidate_transition(int i) {
    // Get message from inbox
    Message msg = states[i].inbox;
    
    // Case 2.1: Process vote grant message
    if (msg.type == MSG_VOTE_GRANT) {
        // Update vote counts
        if (states[i].votes_received[msg.sender] == 0) {
            states[i].true_votes++;
        }
        states[i].fake_votes++;
        states[i].votes_received[msg.sender]++;
        
        // Check if we have majority using appropriate counter
        int votes_needed = N/2 + 1;
        if ((INJECT_DUPLICATE_VOTE_BUG && states[i].fake_votes >= votes_needed) ||
            (!INJECT_DUPLICATE_VOTE_BUG && states[i].true_votes >= votes_needed)) {
            // Become leader
            states[i].role = ROLE_LEADER;
            
            // Clear inbox
            states[i].inbox.type = MSG_NONE;
            states[i].inbox.term = 0;
            states[i].inbox.sender = -1;
            states[i].inbox.sender_role = -1;
            
            // Broadcast heartbeats
            // Removed broadcast logic as we're using abstract message generation
            
            return;
        }
    }
    
    // Case 2.2: Process heartbeat with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[i].current_term) {
        // Become follower
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = msg.term;
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
        states[i].voted_for = -1;
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 2.3: Process vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[i].current_term) {
        // Become follower
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = msg.term;
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
        states[i].voted_for = msg.sender;
        
        // Send vote grant (might be lost)
        if (nondet_bool()) {
            states[msg.sender].inbox.type = MSG_VOTE_GRANT;
            states[msg.sender].inbox.term = states[i].current_term;
            states[msg.sender].inbox.sender = i;
            states[msg.sender].inbox.sender_role = ROLE_FOLLOWER;
        }
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 2.4: No quorum received yet, continue requesting votes
    if ((INJECT_DUPLICATE_VOTE_BUG && states[i].fake_votes <= N/2) ||
        (!INJECT_DUPLICATE_VOTE_BUG && states[i].true_votes <= N/2)) {
        // Removed broadcast logic as we're using abstract message generation
    }
    
    // If message was processed, clear inbox
    if (msg.type != MSG_NONE) {
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
}

// Apply leader transition for node i
void apply_leader_transition(int i) {
    // Get message from inbox
    Message msg = states[i].inbox;
    
    // Case 3.1: Process heartbeat with higher term
    if (msg.type == MSG_HEARTBEAT && msg.term > states[i].current_term) {
        // Become follower
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = msg.term;
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
        states[i].voted_for = -1;
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.2: Process vote request with higher term
    if (msg.type == MSG_VOTE_REQUEST && msg.term > states[i].current_term) {
        // Become follower
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = msg.term;
        if (states[i].current_term > max_term) {
            max_term = states[i].current_term;
        }
        states[i].voted_for = msg.sender;
        
        // Send vote grant (might be lost)
        if (nondet_bool()) {
            states[msg.sender].inbox.type = MSG_VOTE_GRANT;
            states[msg.sender].inbox.term = states[i].current_term;
            states[msg.sender].inbox.sender = i;
            states[msg.sender].inbox.sender_role = ROLE_FOLLOWER;
        }
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.3: Process vote grant message
    if (msg.type == MSG_VOTE_GRANT) {
        // Update vote counts just like in candidate transition
        if (states[i].votes_received[msg.sender] == 0) {
            states[i].true_votes++;
        }
        states[i].fake_votes += 1;
        states[i].votes_received[msg.sender]++;
        
        // Clear inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
        
        return;
    }
    
    // Case 3.4: Send heartbeats periodically
    // Removed heartbeat broadcast logic as we're using abstract message generation
    
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
    msg.type = abs(nondet_int()) % 4;  // 0-3 for message types
    
    if (msg.type == MSG_NONE) {
        states[i].inbox = msg;
        return;
    }
    
    msg.term = abs(nondet_int()) % max_term + 1;
    msg.sender = abs(nondet_int()) % N;
    msg.sender_role = abs(nondet_int()) % 3;
    
    // Check if this message satisfies the constraints
    if (satisfies_constraints(states[i].role, msg, states[i].current_term)) {
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
    int node_i = abs(nondet_int()) % N;
    
    // Non-deterministically handle node activation/deactivation
    if (!states[node_i].active) {
        // Non-deterministically decide whether to activate an inactive node
        if (nondet_bool()) {
            states[node_i].active = 1;
        }
        return;
    } else if (nondet_bool()) {
        // Non-deterministically allow active nodes to become inactive
        states[node_i].active = 0;
        
        // Reset volatile state when node becomes inactive
        // Clear inbox (incoming messages would be lost)
        states[node_i].inbox.type = MSG_NONE;
        states[node_i].inbox.term = 0;
        states[node_i].inbox.sender = -1;
        states[node_i].inbox.sender_role = -1;
        
        // Reset timeout flag (timer would stop)
        states[node_i].timeout = 0;
        
        // Reset votes_received array (volatile election state)
        // Note: We keep current_term and voted_for as they would be persistent state
        if (states[node_i].role == ROLE_CANDIDATE || states[node_i].role == ROLE_LEADER) {
            memset(states[node_i].votes_received, 0, N * sizeof(int));
            states[node_i].true_votes = 0;
            states[node_i].fake_votes = 0;
        }
        
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
}

/* ----- SAFETY PROPERTIES ----- */

// Check if the safety property holds (at most one leader)
_Bool check_safety_property() {
    // Check all pairs of nodes
    for (int i = 0; i < N; i++) {  // #CHECK_THIS
        for (int j = 0; j < N; j++) {  // #CHECK_THIS
            // Skip if same node
            if (i == j) continue;
            
            // Check if both nodes are leaders, in same term
            if (states[i].role == ROLE_LEADER && 
                states[j].role == ROLE_LEADER && 
                states[i].current_term == states[j].current_term) {
                return 0; // Property violated - found two leaders in same term
            }
        }
    }
    
    return 1; // Property holds - no two leaders in same term
}

// Check valid votes property
_Bool check_valid_votes() {
    for (int i = 0; i < N; i++) {
        int true_vote_count = 0;
        int fake_vote_count = 0;
        
        // Count non-zero elements and sum for each node
        for (int j = 0; j < N; j++) {
            if (states[i].votes_received[j] != 0) {
                true_vote_count++;
                fake_vote_count += states[i].votes_received[j];
            }
        }
        
        // Verify that the counts match the stored values
        if (true_vote_count != states[i].true_votes || 
            fake_vote_count != states[i].fake_votes) {
            return 0; // Property violated
        }
    }
    
    return 1; // Property holds
}

// Check unique vote property
_Bool check_unique_vote() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            // Skip if same node or different terms
            if (i == j || states[i].current_term != states[j].current_term) continue;
            
            // Check if both nodes received votes from the same third party
            for (int k = 0; k < N; k++) {
                if (states[i].votes_received[k] > 0 && states[j].votes_received[k] > 0) {
                    return 0; // Property violated - same node voted for two different nodes in same term
                }
            }
        }
    }
    
    return 1; // Property holds - no duplicate votes in same term
}

// Check leader property
_Bool check_leader() {
    int votes_needed = N/2 + 1;
    
    for (int i = 0; i < N; i++) {
        if ((states[i].role == ROLE_LEADER)) {
            if (((!INJECT_DUPLICATE_VOTE_BUG) && states[i].true_votes < votes_needed) ||
                (INJECT_DUPLICATE_VOTE_BUG && states[i].fake_votes < votes_needed)) {
                return 0; // Property violated based on which votes we're checking
            }
        }
    }
    
    return 1; // Property holds - all leaders have enough votes of the right type
}

// _Bool check_leader() {
//     int votes_needed = N/2 + 1;
    
//     for (int i = 0; i < N; i++) {
//         if ((states[i].role == ROLE_LEADER)) {
//             if (states[i].true_votes < votes_needed){
//                 return 0; // Property violated based on which votes we're checking
//             }
//         }
//     }
    
//     return 1; // Property holds - all leaders have enough votes of the right type
// }


// Check voted_for consistency
_Bool check_voted_for() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            // If node i has recorded votes from node j
            if (states[i].votes_received[j] != 0) {
                // Then node j must have voted for node i
                if (states[j].voted_for != i) {
                    return 0; // Property violated - vote record inconsistent with voted_for
                }
            }
        }
    }
    
    return 1; // Property holds - all vote records are consistent
}

// Check that if a node has voted for someone, that node must have recorded the vote
_Bool check_vote_delivered() {
    for (int i = 0; i < N; i++) {
        // If this node has voted for someone
        if (states[i].voted_for != -1) {
            int voted_for = states[i].voted_for;
            // Then that recipient must have recorded this vote
            if (states[voted_for].votes_received[i] == 0) {
                return 0; // Property violated - vote not properly recorded
            }
        }
    }
    
    return 1; // Property holds - all votes are properly recorded
}

// Check that no two different nodes in the same term have majority votes
_Bool check_unique_quorum() {
    int votes_needed = N/2 + 1;
    
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            // Skip if same node or different terms
            if (i == j || states[i].current_term != states[j].current_term) continue;
            
            // Check if both have majority votes, using appropriate vote count based on bug flag
            if ((!INJECT_DUPLICATE_VOTE_BUG && states[i].true_votes >= votes_needed && states[j].true_votes >= votes_needed) ||
                (INJECT_DUPLICATE_VOTE_BUG && states[i].fake_votes >= votes_needed && states[j].fake_votes >= votes_needed)) {
                return 0; // Property violated - two nodes in same term have majority votes
            }
        }
    }
    
    return 1; // Property holds - no overlapping quorums in same term
}

// Generate random state data for a node
void generate_random_state(int node_idx) {
    states[node_idx].node_id = node_idx;
    
    // Use unsigned modulo trick to ensure positive values
    unsigned int rand_role = (unsigned int)nondet_int();
    states[node_idx].role = rand_role % 3; // Random role (0=follower, 1=candidate, 2=leader)
    
    // Ensure current_term is positive (between 1-10)
    unsigned int rand_term = (unsigned int)nondet_int();
    states[node_idx].current_term = (rand_term % 10) + 1;
    
    // Random node voted for or -1
    if (nondet_bool()) {
        unsigned int rand_vote = (unsigned int)nondet_int();
        states[node_idx].voted_for = rand_vote % N;
    } else {
        states[node_idx].voted_for = -1;
    }
    
    states[node_idx].timeout = nondet_bool(); // Random timeout
    states[node_idx].active = nondet_bool(); // Random active status
    
    // Update max_term if needed
    if (states[node_idx].current_term > max_term) {
        max_term = states[node_idx].current_term;
    }
    
    // Generate random votes
    states[node_idx].true_votes = 0;
    states[node_idx].fake_votes = 0;
    
    for (int j = 0; j < N; j++) {
        if (nondet_bool()) { // Randomly decide if this node voted
            // Ensure vote_count is positive (between 1-3)
            unsigned int rand_count = (unsigned int)nondet_int();
            int vote_count = (rand_count % 3) + 1;
            
            // Only if votes_received[j] was previously 0, increment true_votes
            if (states[node_idx].votes_received[j] == 0) {
                states[node_idx].true_votes++; // One unique vote
            }
            
            states[node_idx].votes_received[j] = vote_count;
            states[node_idx].fake_votes += vote_count; // Add to fake votes
        } else {
            states[node_idx].votes_received[j] = 0; // No vote
        }
    }
    
    // Initialize inbox to empty instead of random message
    // since apply_transition will generate messages as needed
    states[node_idx].inbox.type = MSG_NONE;
    states[node_idx].inbox.term = 0;
    states[node_idx].inbox.sender = -1;
    states[node_idx].inbox.sender_role = -1;
}

/* ----- MAIN FUNCTION ----- */

// Initialize states with fixed configuration
void init_states() {
    // Initialize all nodes as followers
    for (int i = 0; i < N; i++) {
        states[i].node_id = i;
        states[i].role = ROLE_FOLLOWER;
        states[i].current_term = 1;
        states[i].voted_for = -1;
        states[i].timeout = 0;
        states[i].active = 1;
        
        // Initialize votes_received array
        memset(states[i].votes_received, 0, N * sizeof(int));
        states[i].true_votes = 0;
        states[i].fake_votes = 0;
        
        // Initialize inbox
        states[i].inbox.type = MSG_NONE;
        states[i].inbox.term = 0;
        states[i].inbox.sender = -1;
        states[i].inbox.sender_role = -1;
    }
    // Set max_term
    max_term = 1;
}

int main() {
    // Generate state for all nodes
    if (USE_FIXED_INIT) {
        // Initialize with fixed configuration
        init_states();
        // left one is leader, right one is not
        // __CPROVER_assume(check_unique_vote());
        // __CPROVER_assume(check_leader());
        // __CPROVER_assume(check_unique_quorum());
        // __CPROVER_assume(check_safety_property());
        // __CPROVER_assume(check_valid_votes());
        // __CPROVER_assume(check_voted_for());
        // __CPROVER_assume(check_vote_delivered());
        
        
        // Apply K transitions when using fixed initialization
        for (int k = 0; k < K; k++) {
            // Apply a transition
            apply_transition();


            // __CPROVER_assume(check_unique_vote());
            // __CPROVER_assume(check_leader());
            // __CPROVER_assume(check_unique_quorum());
            // __CPROVER_assume(check_safety_property());
            // __CPROVER_assume(check_valid_votes());
            // __CPROVER_assume(check_voted_for());
            // __CPROVER_assume(check_vote_delivered());

            if(k==3){
                __CPROVER_assume(!check_unique_vote());
            }
            
        }

        __CPROVER_assume(!check_unique_vote());
        __CPROVER_assume(check_leader());
        __CPROVER_assume(check_unique_quorum());
        // __CPROVER_assume(check_safety_property());

        apply_transition();

        // if(K>=3){
        //     __CPROVER_assume(!check_unique_vote());
        // }

        // if(K>=4){
        //     __CPROVER_assume(!check_leader());
        // }

        // if(K>=7){
        //     __CPROVER_assume(!check_unique_quorum()); // fails at K=7
        // }

        assert(check_safety_property()); // main.assertion.1

    } else {
        // Generate random states
        for (int i = 0; i < N; i++) {
            generate_random_state(i);
        }
        
        // Assume all properties hold for the initial random state
        // __CPROVER_assume(check_valid_votes());
        __CPROVER_assume(check_unique_vote());
        __CPROVER_assume(check_leader());
        __CPROVER_assume(check_safety_property());
        __CPROVER_assume(check_voted_for());
        // __CPROVER_assume(check_vote_delivered());
        __CPROVER_assume(check_unique_quorum());
        // Apply a single transition for random initialization
        apply_transition();
        // __CPROVER_assume(check_valid_votes());
        // __CPROVER_assume(check_voted_for());
        // __CPROVER_assume(check_vote_delivered());
        
        // Assert that safety property still holds after the transition
        // main.assertion.2
        // assert(check_unique_quorum()); 
        assert(check_safety_property());
        // assert(check_leader());
        // assert(check_unique_vote());
    }
    
    return 0;
} 

// to run with bug
// time cbmc raft_cbmc.c --property main.assertion.1 --trace --trace-hex -DINJECT_DUPLICATE_VOTE_BUG -DUSE_FIXED_INIT
// time cbmc raft_cbmc.c --property main.assertion.2 --trace --trace-hex -DINJECT_DUPLICATE_VOTE_BUG 

// to run without bug
// time cbmc raft_cbmc.c --property main.assertion.1 --trace --trace-hex -DUSE_FIXED_INIT
// time cbmc raft_cbmc.c --property main.assertion.2 --trace --trace-hex


// arbitrary 10.48 for both failing and non failing

// fixed, check_leader fails at k=4 and 82.54 seconds




// metric 1, assume at init, and check at each k
