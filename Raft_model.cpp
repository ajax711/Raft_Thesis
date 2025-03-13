#include <iostream>
#include <vector>
#include <queue>
#include <random>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <chrono>
#include <memory>
#include <functional>

// Constants
const double NETWORK_RELIABILITY = 0.7;
const int HEARTBEAT_INTERVAL = 50;
const double NODE_FLAKINESS = 0.1;  // 10% chance a node will be temporarily inactive at any step

// Forward declarations
class Node;
class LogEntry;
class Message;
class Timer;

// Random number generation
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

int randomInt(int min, int max) {
    return std::uniform_int_distribution<int>(min, max)(rng);
}

double randomDouble() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
}

int randomBetween(int min, int max) {
    return min + randomInt(0, max - min);
}

// Enum for node states
enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

// Enum for message types
enum class MessageType {
    REQUEST_VOTE,
    REQUEST_VOTE_RESPONSE,
    APPEND_ENTRIES,
    APPEND_ENTRIES_RESPONSE
};

// Log entry class
class LogEntry {
public:
    int term;
    std::string command;
    int index;

    LogEntry(int t = 0, const std::string& cmd = "", int idx = 0)
        : term(t), command(cmd), index(idx) {}
};

// Timer class
class Timer {
private:
    int expiryTime;
    int duration;
    bool active;

public:
    Timer() : expiryTime(0), duration(0), active(false) {}

    void reset(int newDuration = -1) {
        if (newDuration > 0) {
            duration = newDuration;
        }
        expiryTime = duration + randomInt(-duration/5, duration/5); // Add some randomness
        active = true;
    }

    bool isExpired(int currentTime) const {
        return active && currentTime >= expiryTime;
    }

    void cancel() {
        active = false;
    }

    bool isActive() const {
        return active;
    }
};

// Message class
class Message {
public:
    MessageType type;
    
    // Common fields
    int term;
    int senderId;
    
    // RequestVote specific fields
    int candidateId;
    int lastLogIndex;
    int lastLogTerm;
    
    // AppendEntries specific fields
    int leaderId;
    int prevLogIndex;
    int prevLogTerm;
    std::vector<LogEntry> entries;
    int leaderCommit;
    
    // Response fields
    bool success;
    bool voteGranted;

    Message() : term(0), senderId(0), candidateId(0), lastLogIndex(0), lastLogTerm(0),
                leaderId(0), prevLogIndex(0), prevLogTerm(0), leaderCommit(0),
                success(false), voteGranted(false) {}
};

// Node class
class Node {
private:
    // Helper functions
    void applyToStateMachine(const LogEntry& entry) {
        std::cout << "Node " << id << " applied command: " << entry.command << std::endl;
    }

public:
    // Node identification
    int id;
    NodeState state;
    
    // Persistent state
    int currentTerm;
    int votedFor; // -1 for null
    std::vector<LogEntry> log;
    
    // Volatile state
    int commitIndex;
    int lastApplied;
    
    // Leader-only volatile state
    std::vector<int> nextIndex;
    std::vector<int> matchIndex;
    
    // Communication
    std::queue<Message> inbox;
    
    // Timers
    Timer electionTimeout;
    Timer heartbeatTimer;
    
    // Simulation properties
    bool isActive;
    
    // Election properties
    int votesReceived;

    Node(int nodeId, int numNodes)
        : id(nodeId), state(NodeState::FOLLOWER), currentTerm(0), votedFor(-1),
          commitIndex(0), lastApplied(0), isActive(true), votesReceived(0) {
        
        // Initialize log with dummy entry
        log.push_back(LogEntry(0, "DUMMY", 0));
        
        // Initialize leader-specific data structures
        nextIndex.resize(numNodes, 1);
        matchIndex.resize(numNodes, 0);
        
        // Initialize election timeout
        electionTimeout.reset(randomBetween(150, 300));
    }

    void applyCommittedEntries() {
        while (lastApplied < commitIndex) {
            lastApplied++;
            applyToStateMachine(log[lastApplied]);
        }
    }
};

// Function declarations
void sendRequestVote(Node& candidate, std::vector<Node>& nodes);
void handleRequestVote(Node& node, const Message& message, std::vector<Node>& nodes);
void handleRequestVoteResponse(Node& node, const Message& message, std::vector<Node>& nodes);
void sendAppendEntries(Node& leader, std::vector<Node>& nodes, bool isHeartbeat);
void handleAppendEntries(Node& node, const Message& message, std::vector<Node>& nodes);
void handleAppendEntriesResponse(Node& node, const Message& message, std::vector<Node>& nodes);
void becomeCandidate(Node& node, std::vector<Node>& nodes);
void becomeLeader(Node& node, std::vector<Node>& nodes);
void revertToFollower(Node& node);
void updateCommitIndex(Node& leader, std::vector<Node>& nodes);
void processMessages(std::vector<Node>& nodes);
void handleTimers(std::vector<Node>& nodes, int currentTime);
LogEntry generateRandomLogEntry(int term);
std::string randomCommandString();
void crashNode(Node& node);
void recoverNode(Node& node, int numNodes);
std::vector<std::vector<bool>> createNetworkPartition(const std::vector<Node>& nodes);
bool simulateClientRequest(std::vector<Node>& nodes);
void printSystemState(const std::vector<Node>& nodes);
Node& findNodeById(std::vector<Node>& nodes, int id);
std::vector<Node*> findInactiveNodes(std::vector<Node>& nodes);
void retryAppendEntries(Node& leader, Node& follower);
void runSimulation(int numNodes, int steps);
void processMessagesForNode(Node& node, std::vector<Node>& nodes);
void handleTimerForNode(Node& node, std::vector<Node>& nodes, int currentTime);

// Add a logging utility function at the top of the file
void log_message(const std::string& msg) {
    std::cout << "[" << std::time(nullptr) << "] " << msg << std::endl;
}

// Add this function early in the file, before becomeCandidate and becomeLeader
std::string stateToString(NodeState state) {
    switch (state) {
        case NodeState::FOLLOWER: return "FOLLOWER";
        case NodeState::CANDIDATE: return "CANDIDATE";
        case NodeState::LEADER: return "LEADER";
        default: return "UNKNOWN";
    }
}

// Function implementations
void sendRequestVote(Node& candidate, std::vector<Node>& nodes) {
    int lastLogIndex = candidate.log.size() - 1;
    int lastLogTerm = candidate.log[lastLogIndex].term;
    
    for (auto& node : nodes) {
        if (node.id != candidate.id && node.isActive) {
            // Create RequestVote message
            Message message;
            message.type = MessageType::REQUEST_VOTE;
            message.term = candidate.currentTerm;
            message.senderId = candidate.id;
            message.candidateId = candidate.id;
            message.lastLogIndex = lastLogIndex;
            message.lastLogTerm = lastLogTerm;
            
            // Add to receiver's inbox with probability of delivery
            if (random() < NETWORK_RELIABILITY) {
                node.inbox.push(message);
            }
        }
    }
}

void handleRequestVote(Node& node, const Message& message, std::vector<Node>& nodes) {
    Message response;
    response.type = MessageType::REQUEST_VOTE_RESPONSE;
    response.term = node.currentTerm;
    response.senderId = node.id;
    response.voteGranted = false;
    
    // If term < currentTerm, reject
    if (message.term < node.currentTerm) {
        response.voteGranted = false;
    } else {
        // If term > currentTerm, update term and revert to follower
        if (message.term > node.currentTerm) {
            node.currentTerm = message.term;
            node.state = NodeState::FOLLOWER;
            node.votedFor = -1;
        }
        
        // Check if log is at least as up-to-date as receiver's log
        int lastLogIndex = node.log.size() - 1;
        int lastLogTerm = node.log[lastLogIndex].term;
        
        bool logIsUpToDate = (message.lastLogTerm > lastLogTerm) || 
                           (message.lastLogTerm == lastLogTerm && 
                            message.lastLogIndex >= lastLogIndex);
        
        // Grant vote if:
        // 1. Haven't voted for anyone else this term
        // 2. Candidate's log is at least as up-to-date
        if ((node.votedFor == -1 || node.votedFor == message.candidateId) && logIsUpToDate) {
            node.votedFor = message.candidateId;
            response.voteGranted = true;
            
            // Reset election timeout when granting vote
            node.electionTimeout.reset();
        }
    }
    
    // Send response with probability of delivery
    if (random() < NETWORK_RELIABILITY) {
        Node& senderNode = findNodeById(nodes, message.senderId);
        if (senderNode.isActive) {
            senderNode.inbox.push(response);
        }
    }
}

void handleRequestVoteResponse(Node& node, const Message& message, std::vector<Node>& nodes) {
    // Only process if still a candidate and in same term
    if (node.state == NodeState::CANDIDATE && message.term == node.currentTerm) {
        if (message.voteGranted) {
            node.votesReceived++;
            
            // Check if majority achieved
            if (node.votesReceived > nodes.size() / 2) {
                becomeLeader(node, nodes);
            }
        }
    } else if (message.term > node.currentTerm) {
        // If response contains higher term, update and revert to follower
        node.currentTerm = message.term;
        node.state = NodeState::FOLLOWER;
        node.votedFor = -1;
    }
}

void sendAppendEntries(Node& leader, std::vector<Node>& nodes, bool isHeartbeat) {
    log_message("Leader " + std::to_string(leader.id) + " sending " + 
                (isHeartbeat ? "HEARTBEAT" : "APPEND_ENTRIES") + 
                " (term " + std::to_string(leader.currentTerm) + ")");
    
    for (auto& node : nodes) {
        if (node.id != leader.id && node.isActive) {
            // Create AppendEntries message
            Message message;
            message.type = MessageType::APPEND_ENTRIES;
            message.term = leader.currentTerm;
            message.senderId = leader.id;
            message.leaderId = leader.id;
            message.leaderCommit = leader.commitIndex;
            
            int nextIdx = leader.nextIndex[node.id];
            int prevLogIndex = nextIdx - 1;
            int prevLogTerm = 0;
            
            if (prevLogIndex >= 0 && prevLogIndex < leader.log.size()) {
                prevLogTerm = leader.log[prevLogIndex].term;
            }
            
            message.prevLogIndex = prevLogIndex;
            message.prevLogTerm = prevLogTerm;
            
            // Include new entries (if any) or empty for heartbeat
            if (isHeartbeat) {
                // No entries for heartbeat
            } else {
                // Add entries starting from nextIndex
                for (size_t i = nextIdx; i < leader.log.size(); i++) {
                    message.entries.push_back(leader.log[i]);
                }
                
                // Log entry details
                if (!message.entries.empty()) {
                    log_message("Sending " + std::to_string(message.entries.size()) + 
                                " entries to node " + std::to_string(node.id) + 
                                " (indices " + std::to_string(nextIdx) + "-" + 
                                std::to_string(leader.log.size()-1) + ")");
                }
            }
            
            // Send message with probability based on network reliability
            if (random() < NETWORK_RELIABILITY) {
                node.inbox.push(message);
                log_message("Message delivered to node " + std::to_string(node.id));
            } else {
                log_message("Message to node " + std::to_string(node.id) + " DROPPED");
            }
        }
    }
}

void handleAppendEntries(Node& node, const Message& message, std::vector<Node>& nodes) {
    // Create response message
    Message response;
    response.type = MessageType::APPEND_ENTRIES_RESPONSE;
    response.term = node.currentTerm;
    response.senderId = node.id;
    response.success = false;
    
    // Log term comparison
    log_message("Node " + std::to_string(node.id) + 
               " comparing terms: local=" + std::to_string(node.currentTerm) + 
               " message=" + std::to_string(message.term));
    
    // If message term >= currentTerm, update term
    if (message.term >= node.currentTerm) {
        if (message.term > node.currentTerm) {
            log_message("Node " + std::to_string(node.id) + 
                       " updating term " + std::to_string(node.currentTerm) + 
                       " -> " + std::to_string(message.term));
            node.currentTerm = message.term;
            node.state = NodeState::FOLLOWER;
            node.votedFor = -1;
        }
        
        // Reset election timeout
        node.electionTimeout.reset(randomBetween(150, 300));
        log_message("Node " + std::to_string(node.id) + " reset election timeout");
        
        // Check log consistency
        bool logConsistent = !(message.prevLogIndex >= 0 && 
                              (message.prevLogIndex >= node.log.size() || 
                               node.log[message.prevLogIndex].term != message.prevLogTerm));
        
        log_message("Node " + std::to_string(node.id) + 
                   " log consistency check: " + (logConsistent ? "PASSED" : "FAILED") +
                   " (prevLogIndex=" + std::to_string(message.prevLogIndex) + 
                   ", prevLogTerm=" + std::to_string(message.prevLogTerm) + ")");
        
        if (logConsistent) {
            // Log is consistent - accept AppendEntries
            response.success = true;
            
            // Copy references to entries for response
            response.entries = message.entries;
            
            // Process entries
            if (!message.entries.empty()) {
                log_message("Node " + std::to_string(node.id) + 
                           " appending " + std::to_string(message.entries.size()) + 
                           " entries");
                
                // Delete conflicting entries
                if (message.prevLogIndex + 1 < node.log.size()) {
                    int entriesRemoved = node.log.size() - (message.prevLogIndex + 1);
                    log_message("Node " + std::to_string(node.id) + 
                               " removing " + std::to_string(entriesRemoved) + 
                               " conflicting entries");
                    node.log.resize(message.prevLogIndex + 1);
                }
                
                // Append new entries
                for (const auto& entry : message.entries) {
                    node.log.push_back(entry);
                    log_message("Node " + std::to_string(node.id) + 
                               " appended entry: index=" + std::to_string(entry.index) + 
                               ", term=" + std::to_string(entry.term) + 
                               ", command=\"" + entry.command + "\"");
                }
            }
            
            // Update commit index if leader's commit > local commit
            if (message.leaderCommit > node.commitIndex) {
                int oldCommitIndex = node.commitIndex;
                node.commitIndex = std::min(message.leaderCommit, 
                                           static_cast<int>(node.log.size() - 1));
                
                log_message("Node " + std::to_string(node.id) + 
                           " updated commitIndex " + std::to_string(oldCommitIndex) + 
                           " -> " + std::to_string(node.commitIndex));
                
                // Apply newly committed entries
                node.applyCommittedEntries();
            }
        }
    } else {
        // Message term < currentTerm, reject
        log_message("Node " + std::to_string(node.id) + 
                   " rejected AppendEntries due to lower term");
    }
    
    // Set response term to current term (which might have been updated)
    response.term = node.currentTerm;
    
    // Send response with probability based on network reliability
    if (random() < NETWORK_RELIABILITY) {
        Node& sender = nodes[message.senderId];
        if (sender.isActive) {
            sender.inbox.push(response);
            log_message("Node " + std::to_string(node.id) + 
                       " sent " + (response.success ? "SUCCESS" : "FAILURE") + 
                       " response to node " + std::to_string(message.senderId));
        }
    } else {
        log_message("Node " + std::to_string(node.id) + 
                   " response to node " + std::to_string(message.senderId) + 
                   " DROPPED");
    }
}

void handleAppendEntriesResponse(Node& node, const Message& message, std::vector<Node>& nodes) {
    // Only process if still the leader
    if (node.state == NodeState::LEADER) {
        if (message.term > node.currentTerm) {
            // If response contains higher term, update and revert to follower
            node.currentTerm = message.term;
            node.state = NodeState::FOLLOWER;
            node.votedFor = -1;
        } else if (message.term == node.currentTerm) {
            if (message.success) {
                // Update nextIndex and matchIndex for follower
                int followerId = message.senderId;
                
                // Calculate new indices based on entries sent
                int entriesLength = node.log.size() - node.nextIndex[followerId];
                
                if (entriesLength > 0) {
                    node.nextIndex[followerId] += entriesLength;
                    node.matchIndex[followerId] = node.nextIndex[followerId] - 1;
                }
                
                // Check if we can advance the commit index
                updateCommitIndex(node, nodes);
            } else {
                // If AppendEntries failed because of log inconsistency,
                // decrement nextIndex and retry
                int followerId = message.senderId;
                node.nextIndex[followerId] = std::max(1, node.nextIndex[followerId] - 1);
                
                // Retry immediately with updated nextIndex
                retryAppendEntries(node, findNodeById(nodes, followerId));
            }
        }
    }
}

void becomeCandidate(Node& node, std::vector<Node>& nodes) {
    NodeState oldState = node.state;
    int oldTerm = node.currentTerm;
    
    node.state = NodeState::CANDIDATE;
    node.currentTerm++;
    node.votedFor = node.id;  // Vote for self
    node.votesReceived = 1;   // Count self vote
    
    log_message("Node " + std::to_string(node.id) + 
               " state change: " + stateToString(oldState) + " -> CANDIDATE" +
               ", term: " + std::to_string(oldTerm) + " -> " + 
               std::to_string(node.currentTerm));
    
    // Reset election timeout with randomization
    int timeout = randomBetween(150, 300);
    node.electionTimeout.reset(timeout);
    log_message("Node " + std::to_string(node.id) + 
               " reset election timeout to " + std::to_string(timeout));
    
    // Request votes from all other nodes
    log_message("Node " + std::to_string(node.id) + 
               " starting election for term " + std::to_string(node.currentTerm));
    sendRequestVote(node, nodes);
}

void becomeLeader(Node& node, std::vector<Node>& nodes) {
    NodeState oldState = node.state;
    
    node.state = NodeState::LEADER;
    log_message("Node " + std::to_string(node.id) + 
               " state change: " + stateToString(oldState) + " -> LEADER" +
               " (term " + std::to_string(node.currentTerm) + ")");
    
    // Initialize nextIndex and matchIndex for all followers
    for (size_t i = 0; i < nodes.size(); i++) {
        if (i != node.id) {
            node.nextIndex[i] = node.log.size();
            node.matchIndex[i] = 0;
        }
    }
    
    // Cancel election timeout
    node.electionTimeout.cancel();
    
    // Start heartbeat timer
    node.heartbeatTimer.reset(HEARTBEAT_INTERVAL);
    log_message("Node " + std::to_string(node.id) + 
               " started heartbeat timer at interval " + 
               std::to_string(HEARTBEAT_INTERVAL));
    
    // Send initial heartbeats to establish authority
    log_message("New leader " + std::to_string(node.id) + 
               " sending initial heartbeats");
    sendAppendEntries(node, nodes, true);
}

void revertToFollower(Node& node) {
    node.state = NodeState::FOLLOWER;
    
    // Reset election timeout
    node.electionTimeout.reset(randomBetween(150, 300));
    
    // Cancel heartbeat timer if exists
    if (node.heartbeatTimer.isActive()) {
        node.heartbeatTimer.cancel();
    }
}

void updateCommitIndex(Node& leader, std::vector<Node>& nodes) {
    // Check if there are new entries to commit
    for (size_t N = leader.commitIndex + 1; N < leader.log.size(); N++) {
        // Count replications (including leader itself)
        int replicationCount = 1;  // Leader has the entry
        
        for (const auto& node : nodes) {
            if (node.id != leader.id) {
                if (leader.matchIndex[node.id] >= static_cast<int>(N)) {
                    replicationCount++;
                }
            }
        }
        
        // If majority have replicated AND entry is from current term, commit
        if (replicationCount > static_cast<int>(nodes.size() / 2) && 
            leader.log[N].term == leader.currentTerm) {
            leader.commitIndex = N;
        } else {
            // Can't commit this or any higher entries yet
            break;
        }
    }
    
    // Apply newly committed entries
    leader.applyCommittedEntries();
}

void processMessages(std::vector<Node>& nodes) {
    for (auto& node : nodes) {
        if (node.isActive) {
            while (!node.inbox.empty()) {
                Message message = node.inbox.front();
                node.inbox.pop();
                
                if (message.type == MessageType::REQUEST_VOTE) {
                    handleRequestVote(node, message, nodes);
                } else if (message.type == MessageType::REQUEST_VOTE_RESPONSE) {
                    handleRequestVoteResponse(node, message, nodes);
                } else if (message.type == MessageType::APPEND_ENTRIES) {
                    handleAppendEntries(node, message, nodes);
                } else if (message.type == MessageType::APPEND_ENTRIES_RESPONSE) {
                    handleAppendEntriesResponse(node, message, nodes);
                }
            }
        }
    }
}

void handleTimers(std::vector<Node>& nodes, int currentTime) {
    for (auto& node : nodes) {
        if (node.isActive) {
            // Check election timeout for followers and candidates
            if ((node.state == NodeState::FOLLOWER || node.state == NodeState::CANDIDATE) && 
               node.electionTimeout.isExpired(currentTime)) {
                becomeCandidate(node, nodes);
            }
            
            // Check heartbeat timer for leaders
            if (node.state == NodeState::LEADER && node.heartbeatTimer.isExpired(currentTime)) {
                // Send heartbeats
                sendAppendEntries(node, nodes, true);
                // Reset heartbeat timer
                node.heartbeatTimer.reset(HEARTBEAT_INTERVAL);
            }
        }
    }
}

LogEntry generateRandomLogEntry(int term) {
    LogEntry entry;
    entry.term = term;
    entry.command = randomCommandString();
    return entry;
}

/**
 * Generates a random 10-letter string
 * Returns a gurbled up string with no particular meaning
 */
std::string randomCommandString() {
    static const std::string charset = 
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    std::string randomStr;
    randomStr.reserve(10);
    
    for (int i = 0; i < 10; i++) {
        int randomIndex = randomInt(0, charset.size() - 1);
        randomStr += charset[randomIndex];
    }
    
    return randomStr;
}

void crashNode(Node& node) {
    node.isActive = false;
}

void recoverNode(Node& node, int numNodes) {
    node.isActive = true;
    node.state = NodeState::FOLLOWER;
    
    // Clear inbox
    std::queue<Message> empty;
    std::swap(node.inbox, empty);
    
    // Don't reset persistent state (currentTerm, votedFor, log)
    
    // Reset volatile state
    node.commitIndex = 0;
    node.lastApplied = 0;
    
    // Restart election timeout
    node.electionTimeout.reset(randomBetween(150, 300));
}

std::vector<std::vector<bool>> createNetworkPartition(const std::vector<Node>& nodes) {
    // Create two partitions
    int partition1Size = randomInt(1, nodes.size() - 1);
    
    // Create a matrix of which nodes can communicate
    std::vector<std::vector<bool>> canCommunicate(nodes.size(), std::vector<bool>(nodes.size(), false));
    
    for (size_t i = 0; i < nodes.size(); i++) {
        for (size_t j = 0; j < nodes.size(); j++) {
            if (i < partition1Size && j < partition1Size) {
                canCommunicate[i][j] = true;  // Nodes in partition 1 can communicate
            } else if (i >= partition1Size && j >= partition1Size) {
                canCommunicate[i][j] = true;  // Nodes in partition 2 can communicate
            } else {
                canCommunicate[i][j] = false; // Nodes in different partitions cannot communicate
            }
        }
    }
    
    return canCommunicate;
}

/**
 * Simulates a client sending a request to the current leader
 * Returns true if request was successfully sent to a leader
 */
bool simulateClientRequest(std::vector<Node>& nodes) {
    // Find the current leader
    Node* leader = nullptr;
    for (auto& node : nodes) {
        if (node.state == NodeState::LEADER && node.isActive) {
            leader = &node;
            break;
        }
    }
    
    if (leader == nullptr) {
        log_message("CLIENT REQUEST FAILED: No active leader found");
        return false;
    }
    
    // Generate random 10-letter string
    std::string randomStr = randomCommandString();
    
    log_message("CLIENT REQUEST to leader " + std::to_string(leader->id) + 
               ": \"" + randomStr + "\"");
    
    // Create log entry
    LogEntry entry(leader->currentTerm, randomStr, leader->log.size());
    
    // Append to leader's log
    leader->log.push_back(entry);
    log_message("Leader " + std::to_string(leader->id) + 
               " appended entry: index=" + std::to_string(entry.index) + 
               ", term=" + std::to_string(entry.term) + 
               ", command=\"" + entry.command + "\"");
    
    // Try to replicate to followers
    sendAppendEntries(*leader, nodes, false);
    
    return true;
}

void printSystemState(const std::vector<Node>& nodes) {
    std::cout << "=== System State ===" << std::endl;
    for (const auto& node : nodes) {
        std::string stateStr;
        switch (node.state) {
            case NodeState::FOLLOWER: stateStr = "FOLLOWER"; break;
            case NodeState::CANDIDATE: stateStr = "CANDIDATE"; break;
            case NodeState::LEADER: stateStr = "LEADER"; break;
        }
        
        std::cout << "Node " << node.id << ": " 
                  << (node.isActive ? "ACTIVE" : "CRASHED") 
                  << ", State: " << stateStr
                  << ", Term: " << node.currentTerm 
                  << ", Log Size: " << node.log.size() - 1  // -1 for dummy entry
                  << ", Commit Index: " << node.commitIndex
                  << std::endl;
    }
    std::cout << "===================" << std::endl;
}

Node& findNodeById(std::vector<Node>& nodes, int id) {
    for (auto& node : nodes) {
        if (node.id == id) {
            return node;
        }
    }
    throw std::runtime_error("Node not found with ID: " + std::to_string(id));
}

std::vector<Node*> findInactiveNodes(std::vector<Node>& nodes) {
    std::vector<Node*> inactiveNodes;
    for (auto& node : nodes) {
        if (!node.isActive) {
            inactiveNodes.push_back(&node);
        }
    }
    return inactiveNodes;
}

void retryAppendEntries(Node& leader, Node& follower) {
    if (leader.state == NodeState::LEADER && leader.isActive && follower.isActive) {
        // Create AppendEntries message
        Message message;
        message.type = MessageType::APPEND_ENTRIES;
        message.term = leader.currentTerm;
        message.senderId = leader.id;
        message.leaderId = leader.id;
        message.leaderCommit = leader.commitIndex;
        
        int nextIdx = leader.nextIndex[follower.id];
        int prevLogIndex = nextIdx - 1;
        int prevLogTerm = leader.log[prevLogIndex].term;
        
        message.prevLogIndex = prevLogIndex;
        message.prevLogTerm = prevLogTerm;
        
        // Include entries from nextIndex
        for (size_t i = nextIdx; i < leader.log.size(); i++) {
            message.entries.push_back(leader.log[i]);
        }
        
        // Send to follower with probability of delivery
        if (random() < NETWORK_RELIABILITY) {
            follower.inbox.push(message);
        }
    }
}

void runSimulation(int numNodes, int steps) {
    // Initialize system
    std::vector<Node> nodes;
    for (int i = 0; i < numNodes; i++) {
        nodes.emplace_back(i, numNodes);
    }
    
    // Start one node as leader (node 0)
    Node& initialLeader = nodes[0];
    initialLeader.state = NodeState::LEADER;
    initialLeader.currentTerm = 1;  // Start with term 1
    
    // Initialize leader-specific state
    for (int i = 0; i < numNodes; i++) {
        if (i != initialLeader.id) {
            initialLeader.nextIndex[i] = 1;  // Start with first entry
            initialLeader.matchIndex[i] = 0;
        }
    }
    
    // Set up heartbeat timer for leader
    initialLeader.heartbeatTimer.reset(HEARTBEAT_INTERVAL);
    initialLeader.electionTimeout.cancel();
    
    // Send initial heartbeats to establish authority
    sendAppendEntries(initialLeader, nodes, true);
    
    std::vector<std::vector<bool>> networkPartition;
    bool partitioned = false;
    int currentTime = 0;
    
    for (int step = 1; step <= steps; step++) {
        currentTime++;
        log_message("=============== STEP " + std::to_string(step) + " ===============");
        
        // 1. Process messages, but check for temporary inactivity for each node
        for (auto& node : nodes) {
            if (node.isActive) {
                if (random() > NODE_FLAKINESS) {
                    processMessagesForNode(node, nodes);
                } else {
                    log_message("Node " + std::to_string(node.id) + " TEMPORARY INACTIVE this step");
                }
            }
        }
        
        // 2. Handle timers, but check for temporary inactivity for each node
        for (auto& node : nodes) {
            if (node.isActive && random() > NODE_FLAKINESS) {
                handleTimerForNode(node, nodes, currentTime);
            }
        }
        
        // 3. Randomly simulate events
        if (random() < 0.01) {  // 1% chance of node failure
            int nodeId = randomInt(0, nodes.size() - 1);
            crashNode(nodes[nodeId]);
            log_message("Node " + std::to_string(nodeId) + " crashed");
        }
        
        if (random() < 0.01) {  // 1% chance of node recovery
            auto inactiveNodes = findInactiveNodes(nodes);
            if (!inactiveNodes.empty()) {
                int idx = randomInt(0, inactiveNodes.size() - 1);
                recoverNode(*inactiveNodes[idx], numNodes);
                log_message("Node " + std::to_string(inactiveNodes[idx]->id) + " recovered");
            }
        }
        
        if (random() < 0.005 && !partitioned) {  // 0.5% chance of network partition
            networkPartition = createNetworkPartition(nodes);
            partitioned = true;
            log_message("Network partitioned");
        }
        
        if (random() < 0.01 && partitioned) {  // 1% chance of healing network partition
            partitioned = false;
            log_message("Network partition healed");
        }
        
        if (random() < 0.05) {  // 5% chance of client request
            bool success = simulateClientRequest(nodes);
            if (success) {
                log_message("Client request succeeded");
            } else {
                log_message("Client request failed - no leader available");
            }
        }
        
        // 4. Print state for visualization
        if (step % 10 == 0) {
            printSystemState(nodes);
        }
    }
}

void processMessagesForNode(Node& node, std::vector<Node>& nodes) {
    if (!node.inbox.empty()) {
        log_message("Node " + std::to_string(node.id) + " processing " + 
                   std::to_string(node.inbox.size()) + " messages");
    }
    
    while (!node.inbox.empty()) {
        Message message = node.inbox.front();
        node.inbox.pop();
        
        std::string msgType;
        switch (message.type) {
            case MessageType::REQUEST_VOTE: msgType = "REQUEST_VOTE"; break;
            case MessageType::REQUEST_VOTE_RESPONSE: msgType = "REQUEST_VOTE_RESPONSE"; break;
            case MessageType::APPEND_ENTRIES: msgType = "APPEND_ENTRIES"; break;
            case MessageType::APPEND_ENTRIES_RESPONSE: msgType = "APPEND_ENTRIES_RESPONSE"; break;
        }
        
        log_message("Node " + std::to_string(node.id) + " handling " + msgType + 
                   " from node " + std::to_string(message.senderId) + 
                   " (term " + std::to_string(message.term) + ")");
        
        if (message.type == MessageType::REQUEST_VOTE) {
            handleRequestVote(node, message, nodes);
        }
        else if (message.type == MessageType::REQUEST_VOTE_RESPONSE) {
            handleRequestVoteResponse(node, message, nodes);
        }
        else if (message.type == MessageType::APPEND_ENTRIES) {
            handleAppendEntries(node, message, nodes);
        }
        else if (message.type == MessageType::APPEND_ENTRIES_RESPONSE) {
            handleAppendEntriesResponse(node, message, nodes);
        }
    }
}

void handleTimerForNode(Node& node, std::vector<Node>& nodes, int currentTime) {
    // Election timeout for followers and candidates
    if ((node.state == NodeState::FOLLOWER || node.state == NodeState::CANDIDATE) && 
        node.electionTimeout.isExpired(currentTime)) {
        becomeCandidate(node, nodes);
    }
    
    // Heartbeat timer for leaders
    if (node.state == NodeState::LEADER && node.heartbeatTimer.isExpired(currentTime)) {
        sendAppendEntries(node, nodes, true);
        node.heartbeatTimer.reset(HEARTBEAT_INTERVAL);
    }
}

int main() {
    int numNodes = 5;
    int simulationSteps = 1000;
    
    std::cout << "Starting Raft simulation with " << numNodes << " nodes for " 
              << simulationSteps << " steps" << std::endl;
    
    runSimulation(numNodes, simulationSteps);
    
    return 0;
}
`