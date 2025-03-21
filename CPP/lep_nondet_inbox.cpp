#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

// Model parameters
#define MAX_NODES 3
#define INITIAL_LEADER 0

// Roles enum
typedef enum {FOLLOWER, CANDIDATE, LEADER} Role;

// Message types enum
typedef enum {NONE, HEARTBEAT, VOTE_REQUEST, VOTE_GRANT} MessageType;

// Message structure
typedef struct {
    MessageType type;
    int term;
    int sender_id;
} Message;

// Node structure
typedef struct {
    int node_id;
    Message inbox;
    int voted_for;
    int current_term;
    Role role;
    bool votes_received[MAX_NODES];
    bool timeout;
} Node;

// Global variables
Node nodes[MAX_NODES];
bool message_loss;

// Non-deterministic functions
bool nondet_bool();
int nondet_int();
int nondet_node_idx();

// Function prototypes
void send_message(int source_node, int target_node, Message message);
void broadcast(int source_node, Message message);
void non_deterministic_change(const char* variable_name, int node_id, int new_value);
void initialize_nodes();
void follower_behavior(int node_idx);
void leader_behavior(int node_idx);
void candidate_behavior(int node_idx);
int count_votes(int node_idx);

// Initialize all nodes
void initialize_nodes() {
    for (int i = 0; i < MAX_NODES; i++) {
        nodes[i].node_id = i;
        nodes[i].inbox.type = NONE;
        nodes[i].voted_for = -1; // -1 represents null
        nodes[i].current_term = 1;
        nodes[i].role = (i == INITIAL_LEADER) ? LEADER : FOLLOWER;
        nodes[i].timeout = false;
        
        for (int j = 0; j < MAX_NODES; j++) {
            nodes[i].votes_received[j] = false;
        }
    }
}

// Send a message from one node to another with possible message loss
void send_message(int source_node, int target_node, Message message) {
    // Message loss is non-deterministic
    message_loss = nondet_bool();
    if (!message_loss && target_node >= 0 && target_node < MAX_NODES) {
        // Only deliver if inbox is empty (simplification)
        if (nodes[target_node].inbox.type == NONE) {
            nodes[target_node].inbox = message;
        }
    }
}

// Broadcast a message to all nodes
void broadcast(int source_node, Message message) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (i != source_node) {
            send_message(source_node, i, message);
        }
    }
}

// Non-deterministic state change for modeling amnesia
void non_deterministic_change(const char* variable_name, int node_id, int new_value) {
    bool amnesia = nondet_bool(); // Non-deterministic amnesia
    
    if (!amnesia && node_id >= 0 && node_id < MAX_NODES) {
        if (strcmp(variable_name, "voted_for") == 0) {
            nodes[node_id].voted_for = new_value;
        }
        // Other variables could be added here
    }
}

// Count total votes received by a candidate
int count_votes(int node_idx) {
    int total_votes = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[node_idx].votes_received[i]) {
            total_votes++;
        }
    }
    return total_votes;
}

// Follower behavior implementation
void follower_behavior(int node_idx) {
    Node* node = &nodes[node_idx];
    
    // Case 1.1: Empty inbox with timeout
    if (node->inbox.type == NONE && nondet_bool()) { // Non-deterministic timeout
        node->timeout = true;
        node->current_term++;
        node->voted_for = -1; // Reset voted_for when term changes
        node->voted_for = node->node_id; // Vote for self
        
        // Reset vote array
        for (int i = 0; i < MAX_NODES; i++) {
            node->votes_received[i] = false;
        }
        node->votes_received[node_idx] = true; // Vote for self
        
        node->role = CANDIDATE;
        
        // Broadcast vote request
        Message vote_req = {VOTE_REQUEST, node->current_term, node->node_id};
        broadcast(node_idx, vote_req);
    }
    // Case 1.2: Inbox contains vote request
    else if (node->inbox.type == VOTE_REQUEST) {
        node->timeout = false;
        
        if (node->inbox.term >= node->current_term && node->voted_for == -1) {
            // Create vote grant message
            Message vote_grant = {VOTE_GRANT, node->current_term, node->node_id};
            send_message(node_idx, node->inbox.sender_id, vote_grant);
            
            // Non-deterministically set voted_for
            non_deterministic_change("voted_for", node_idx, node->inbox.sender_id);
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    // Case 1.3: Inbox contains heartbeat with valid term
    else if (node->inbox.type == HEARTBEAT && node->inbox.term >= node->current_term) {
        node->timeout = false;
        
        if (node->inbox.term > node->current_term) {
            node->current_term = node->inbox.term;
            node->voted_for = -1;
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    // Case 1.4: Inbox contains heartbeat with stale term
    else if (node->inbox.type == HEARTBEAT && node->inbox.term < node->current_term) {
        node->timeout = true;
        node->current_term++;
        node->voted_for = -1; // Reset voted_for when term changes
        node->role = CANDIDATE;
        node->voted_for = node->node_id; // Vote for self
        
        // Reset vote array
        for (int i = 0; i < MAX_NODES; i++) {
            node->votes_received[i] = false;
        }
        node->votes_received[node_idx] = true; // Vote for self
        
        // Broadcast vote request
        Message vote_req = {VOTE_REQUEST, node->current_term, node->node_id};
        broadcast(node_idx, vote_req);
        
        // Clear inbox
        node->inbox.type = NONE;
    }
}

// Leader behavior implementation
void leader_behavior(int node_idx) {
    Node* node = &nodes[node_idx];
    
    // Case 2.1: Normal operation - broadcast heartbeat
    if (nondet_bool()) { // Non-deterministic heartbeat trigger
        Message heartbeat = {HEARTBEAT, node->current_term, node->node_id};
        broadcast(node_idx, heartbeat);
    }
    
    // Case 2.2: Received message with higher term (heartbeat)
    if (node->inbox.type == HEARTBEAT && node->inbox.term > node->current_term) {
        node->current_term = node->inbox.term;
        node->role = FOLLOWER;
        node->voted_for = -1;
        
        // Reset vote array
        for (int i = 0; i < MAX_NODES; i++) {
            node->votes_received[i] = false;
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    
    // Case 2.3: Received vote request with higher term
    else if (node->inbox.type == VOTE_REQUEST && node->inbox.term > node->current_term) {
        node->current_term = node->inbox.term;
        node->role = FOLLOWER;
        node->voted_for = -1;
        
        // Reset vote array
        for (int i = 0; i < MAX_NODES; i++) {
            node->votes_received[i] = false;
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
}

// Candidate behavior implementation
void candidate_behavior(int node_idx) {
    Node* node = &nodes[node_idx];
    
    // Case 3.1: Received vote
    if (node->inbox.type == VOTE_GRANT) {
        int sender_id = node->inbox.sender_id;
        if (sender_id >= 0 && sender_id < MAX_NODES) {
            node->votes_received[sender_id] = true;
            
            // Count total votes
            int total_votes = count_votes(node_idx);
            
            if (total_votes > MAX_NODES / 2) {
                // Reset vote array
                for (int i = 0; i < MAX_NODES; i++) {
                    node->votes_received[i] = false;
                }
                
                node->role = LEADER;
                
                // Broadcast heartbeat as new leader
                Message heartbeat = {HEARTBEAT, node->current_term, node->node_id};
                broadcast(node_idx, heartbeat);
            } else {
                // Broadcast vote request again
                Message vote_req = {VOTE_REQUEST, node->current_term, node->node_id};
                broadcast(node_idx, vote_req);
            }
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    
    // Case 3.2: Received heartbeat with higher term
    else if (node->inbox.type == HEARTBEAT && node->inbox.term > node->current_term) {
        node->current_term = node->inbox.term;
        node->role = FOLLOWER;
        node->voted_for = -1;
        
        // Reset vote array
        for (int i = 0; i < MAX_NODES; i++) {
            node->votes_received[i] = false;
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    
    // Case 3.3: Received vote request
    else if (node->inbox.type == VOTE_REQUEST) {
        if (node->inbox.term > node->current_term) {
            node->current_term = node->inbox.term;
            node->role = FOLLOWER;
            
            // Non-deterministically set voted_for
            non_deterministic_change("voted_for", node_idx, node->inbox.sender_id);
            
            // Send vote
            Message vote = {VOTE_GRANT, node->current_term, node->node_id};
            send_message(node_idx, node->inbox.sender_id, vote);
            
            // Reset vote array
            for (int i = 0; i < MAX_NODES; i++) {
                node->votes_received[i] = false;
            }
        }
        
        // Clear inbox
        node->inbox.type = NONE;
    }
    
    // Case 3.4: No quorum received yet (active timeout)
    else if (nondet_bool() && count_votes(node_idx) <= MAX_NODES / 2) {
        // Broadcast vote request again
        Message vote_req = {VOTE_REQUEST, node->current_term, node->node_id};
        broadcast(node_idx, vote_req);
    }
}

// Main function with asynchronous transitions
int main() {
    // Initialize all nodes
    initialize_nodes();
    
    // Non-deterministically run the protocol
    while (nondet_bool()) {
        // Non-deterministically select a node to execute a transition
        int node_idx = nondet_node_idx();
        
        // Ensure node_idx is valid
        __CPROVER_assume(node_idx >= 0 && node_idx < MAX_NODES);
        
        // Execute the appropriate behavior based on the node's role
        if (nodes[node_idx].role == FOLLOWER) {
            follower_behavior(node_idx);
        } else if (nodes[node_idx].role == LEADER) {
            leader_behavior(node_idx);
        } else if (nodes[node_idx].role == CANDIDATE) {
            candidate_behavior(node_idx);
        }
    }
    
    // Safety property: There is at most one leader per term
    for (int i = 0; i < MAX_NODES; i++) {
        for (int j = i + 1; j < MAX_NODES; j++) {
            if (nodes[i].role == LEADER && nodes[j].role == LEADER) {
                assert(nodes[i].current_term != nodes[j].current_term);
            }
        }
    }
    
    return 0;
} 

