# Raft Protocol Simulation Model

## 1. Data Structures

### 1.1 Node State
```
Node {
    // Node identification
    id: Integer                  // Unique identifier for the node
    state: Enum {FOLLOWER, CANDIDATE, LEADER}
    
    // Persistent state (updated on stable storage before responding to RPCs)
    currentTerm: Integer         // Latest term seen (initialized to 0)
    votedFor: Integer or null    // ID of candidate voted for in current term (or null)
    log: Array of LogEntry       // Log entries (first index is 1)
    
    // Volatile state
    commitIndex: Integer         // Index of highest log entry known to be committed (init to 0)
    lastApplied: Integer         // Index of highest log entry applied to state machine (init to 0)
    
    // Leader-only volatile state (reinitialized after election)
    nextIndex: Array of Integer  // For each server, index of next log entry to send
    matchIndex: Array of Integer // For each server, index of highest log entry known to be replicated
    
    // Communication
    inbox: Queue of Message      // Incoming messages queue
    
    // Timers
    electionTimeout: Timer       // Timer for election timeout
    heartbeatTimer: Timer        // Timer for sending periodic heartbeats (leaders only)
    
    // Simulation properties
    isActive: Boolean            // Whether node is active or has crashed
}
```

### 1.2 Log Entry
```
LogEntry {
    term: Integer                // Term when entry was received by leader
    command: String              // Command for state machine (simulated as string)
    index: Integer               // Position in the log (1-based)
}
```

### 1.3 Message Types
```
Message {
    type: Enum {REQUEST_VOTE, REQUEST_VOTE_RESPONSE, 
               APPEND_ENTRIES, APPEND_ENTRIES_RESPONSE}
    
    // Common fields
    term: Integer                // Term of the sender
    senderId: Integer            // ID of the sending node
    
    // RequestVote specific fields
    candidateId: Integer         // Candidate requesting vote
    lastLogIndex: Integer        // Index of candidate's last log entry
    lastLogTerm: Integer         // Term of candidate's last log entry
    
    // AppendEntries specific fields
    leaderId: Integer            // Current leader's ID
    prevLogIndex: Integer        // Index of log entry preceding new ones
    prevLogTerm: Integer         // Term of prevLogIndex entry
    entries: Array of LogEntry   // Log entries to store (empty for heartbeat)
    leaderCommit: Integer        // Leader's commitIndex
    
    // Response fields
    success: Boolean             // True if operation succeeded
    voteGranted: Boolean         // True if vote was granted
}
```

### 1.4 Timer
```
Timer {
    expiryTime: Integer          // When the timer expires (simulation time units)
    duration: Integer            // Base duration of the timer
    isActive: Boolean            // Whether timer is currently running
    
    reset(): Resets the timer to a new expiry time, optionally with randomization
    isExpired(): Returns true if current time > expiryTime
    cancel(): Stops the timer
}
```

## 2. Initialization & Setup

### 2.1 System Initialization
```
function initializeSystem(numNodes):
    nodes = new Array(numNodes)
    
    for i from 0 to numNodes-1:
        nodes[i] = createNode(i)
    
    return nodes
```

### 2.2 Node Creation
```
function createNode(id):
    node = new Node()
    node.id = id
    node.state = FOLLOWER
    node.currentTerm = 0
    node.votedFor = null
    node.log = []
    node.log[0] = createDummyLogEntry()  // Index 0 contains dummy entry
    node.commitIndex = 0
    node.lastApplied = 0
    node.nextIndex = []
    node.matchIndex = []
    node.inbox = new Queue()
    node.isActive = true
    
    // Initialize election timeout with random value
    node.electionTimeout = new Timer()
    node.electionTimeout.reset(randomBetween(150, 300))  // ms
    
    return node
```

## 3. Core Protocol Functions

### 3.1 RequestVote RPC

#### 3.1.1 Sending RequestVote
```
function sendRequestVote(candidate, nodes):
    lastLogIndex = candidate.log.length - 1
    lastLogTerm = candidate.log[lastLogIndex].term
    
    for each node in nodes:
        if node.id != candidate.id && node.isActive:
            // Create RequestVote message
            message = new Message()
            message.type = REQUEST_VOTE
            message.term = candidate.currentTerm
            message.senderId = candidate.id
            message.candidateId = candidate.id
            message.lastLogIndex = lastLogIndex
            message.lastLogTerm = lastLogTerm
            
            // Add to receiver's inbox with probability of delivery
            if random() < NETWORK_RELIABILITY:
                node.inbox.enqueue(message)
```

#### 3.1.2 Handling RequestVote
```
function handleRequestVote(node, message, nodes):
    response = new Message()
    response.type = REQUEST_VOTE_RESPONSE
    response.term = node.currentTerm
    response.senderId = node.id
    response.voteGranted = false
    
    // If term < currentTerm, reject
    if message.term < node.currentTerm:
        response.voteGranted = false
    else:
        // If term > currentTerm, update term and revert to follower
        if message.term > node.currentTerm:
            node.currentTerm = message.term
            node.state = FOLLOWER
            node.votedFor = null
        
        // Check if log is at least as up-to-date as receiver's log
        lastLogIndex = node.log.length - 1
        lastLogTerm = node.log[lastLogIndex].term
        
        logIsUpToDate = (message.lastLogTerm > lastLogTerm) || 
                       (message.lastLogTerm == lastLogTerm && 
                        message.lastLogIndex >= lastLogIndex)
        
        // Grant vote if:
        // 1. Haven't voted for anyone else this term
        // 2. Candidate's log is at least as up-to-date
        if (node.votedFor == null || node.votedFor == message.candidateId) && logIsUpToDate:
            node.votedFor = message.candidateId
            response.voteGranted = true
            
            // Reset election timeout when granting vote
            node.electionTimeout.reset()
    
    // Send response with probability of delivery
    if random() < NETWORK_RELIABILITY:
        senderNode = findNodeById(nodes, message.senderId)
        if senderNode.isActive:
            senderNode.inbox.enqueue(response)
```

#### 3.1.3 Handling RequestVote Response
```
function handleRequestVoteResponse(node, message, nodes):
    // Only process if still a candidate and in same term
    if node.state == CANDIDATE && message.term == node.currentTerm:
        if message.voteGranted:
            node.votesReceived += 1
            
            // Check if majority achieved
            if node.votesReceived > nodes.length / 2:
                becomeLeader(node, nodes)
    else if message.term > node.currentTerm:
        // If response contains higher term, update and revert to follower
        node.currentTerm = message.term
        node.state = FOLLOWER
        node.votedFor = null
```

### 3.2 AppendEntries RPC

#### 3.2.1 Sending AppendEntries
```
function sendAppendEntries(leader, nodes, isHeartbeat):
    for each node in nodes:
        if node.id != leader.id && node.isActive:
            // Create AppendEntries message
            message = new Message()
            message.type = APPEND_ENTRIES
            message.term = leader.currentTerm
            message.senderId = leader.id
            message.leaderId = leader.id
            message.leaderCommit = leader.commitIndex
            
            nextIndex = leader.nextIndex[node.id]
            prevLogIndex = nextIndex - 1
            prevLogTerm = leader.log[prevLogIndex].term
            
            message.prevLogIndex = prevLogIndex
            message.prevLogTerm = prevLogTerm
            
            // Include new entries (if any) or empty array for heartbeat
            if isHeartbeat:
                message.entries = []
            else:
                message.entries = leader.log.slice(nextIndex)
            
            // Add to receiver's inbox with probability of delivery
            if random() < NETWORK_RELIABILITY:
                node.inbox.enqueue(message)
```

#### 3.2.2 Handling AppendEntries
```
function handleAppendEntries(node, message, nodes):
    response = new Message()
    response.type = APPEND_ENTRIES_RESPONSE
    response.term = node.currentTerm
    response.senderId = node.id
    response.success = false
    
    // If term < currentTerm, reject
    if message.term < node.currentTerm:
        response.success = false
    else:
        // Valid message from current or newer leader, reset election timeout
        node.electionTimeout.reset()
        
        // If term > currentTerm, update term
        if message.term > node.currentTerm:
            node.currentTerm = message.term
            node.votedFor = null
        
        // If candidate, revert to follower
        if node.state == CANDIDATE:
            node.state = FOLLOWER
        
        // Check log consistency
        if node.log.length <= message.prevLogIndex ||
           node.log[message.prevLogIndex].term != message.prevLogTerm:
            // Log inconsistency
            response.success = false
        else:
            // Log consistent, process entries
            response.success = true
            
            // If conflicting entries exist, delete them and all that follow
            if message.entries.length > 0:
                for i from 0 to message.entries.length - 1:
                    logIndex = message.prevLogIndex + 1 + i
                    
                    if logIndex < node.log.length:
                        if node.log[logIndex].term != message.entries[i].term:
                            // Delete conflicting entry and all that follow
                            node.log = node.log.slice(0, logIndex)
                            break
                
                // Append any new entries not already in the log
                for entry in message.entries:
                    if entry.index >= node.log.length:
                        node.log.push(entry)
            
            // Update commitIndex if leader's commitIndex > local commitIndex
            if message.leaderCommit > node.commitIndex:
                node.commitIndex = min(message.leaderCommit, node.log.length - 1)
                
                // Apply newly committed entries
                applyCommittedEntries(node)
    
    // Send response with probability of delivery
    if random() < NETWORK_RELIABILITY:
        senderNode = findNodeById(nodes, message.senderId)
        if senderNode.isActive:
            senderNode.inbox.enqueue(response)
```

#### 3.2.3 Handling AppendEntries Response
```
function handleAppendEntriesResponse(node, message, nodes):
    // Only process if still the leader
    if node.state == LEADER:
        if message.term > node.currentTerm:
            // If response contains higher term, update and revert to follower
            node.currentTerm = message.term
            node.state = FOLLOWER
            node.votedFor = null
        else if message.term == node.currentTerm:
            if message.success:
                // Update nextIndex and matchIndex for follower
                followerId = message.senderId
                
                // Calculate new indices based on entries sent
                entriesLength = node.log.length - node.nextIndex[followerId]
                
                if entriesLength > 0:
                    node.nextIndex[followerId] += entriesLength
                    node.matchIndex[followerId] = node.nextIndex[followerId] - 1
                
                // Check if we can advance the commit index
                updateCommitIndex(node, nodes)
            else:
                // If AppendEntries failed because of log inconsistency,
                // decrement nextIndex and retry
                followerId = message.senderId
                node.nextIndex[followerId] = max(1, node.nextIndex[followerId] - 1)
                
                // Retry immediately with updated nextIndex
                retryAppendEntries(node, findNodeById(nodes, followerId))
```

### 3.3 State Transitions

#### 3.3.1 Become Candidate
```
function becomeCandidate(node, nodes):
    node.state = CANDIDATE
    node.currentTerm += 1
    node.votedFor = node.id  // Vote for self
    node.votesReceived = 1   // Count self vote
    
    // Reset election timeout with randomization
    node.electionTimeout.reset(randomBetween(150, 300))
    
    // Request votes from all other nodes
    sendRequestVote(node, nodes)
```

#### 3.3.2 Become Leader
```
function becomeLeader(node, nodes):
    node.state = LEADER
    
    // Initialize nextIndex and matchIndex for all followers
    for each otherNode in nodes:
        if otherNode.id != node.id:
            node.nextIndex[otherNode.id] = node.log.length
            node.matchIndex[otherNode.id] = 0
    
    // Cancel election timeout
    node.electionTimeout.cancel()
    
    // Start heartbeat timer
    node.heartbeatTimer = new Timer()
    node.heartbeatTimer.reset(HEARTBEAT_INTERVAL)  // Typically 50ms
    
    // Send initial empty AppendEntries (heartbeats) to establish authority
    sendAppendEntries(node, nodes, true)
```

#### 3.3.3 Revert to Follower
```
function revertToFollower(node):
    node.state = FOLLOWER
    
    // Reset election timeout
    node.electionTimeout.reset(randomBetween(150, 300))
    
    // Cancel heartbeat timer if exists
    if node.heartbeatTimer:
        node.heartbeatTimer.cancel()
        node.heartbeatTimer = null
```

### 3.4 Log Consistency

#### 3.4.1 Update Commit Index
```
function updateCommitIndex(leader, nodes):
    // Check if there are new entries to commit
    for N from leader.commitIndex + 1 to leader.log.length - 1:
        // Count replications (including leader itself)
        replicationCount = 1  // Leader has the entry
        
        for each otherNode in nodes:
            if otherNode.id != leader.id:
                if leader.matchIndex[otherNode.id] >= N:
                    replicationCount += 1
        
        // If majority have replicated AND entry is from current term, commit
        if replicationCount > nodes.length / 2 && leader.log[N].term == leader.currentTerm:
            leader.commitIndex = N
        else:
            // Can't commit this or any higher entries yet
            break
    
    // Apply newly committed entries
    applyCommittedEntries(leader)
```

#### 3.4.2 Apply Committed Entries
```
function applyCommittedEntries(node):
    while node.lastApplied < node.commitIndex:
        node.lastApplied += 1
        applyToStateMachine(node, node.log[node.lastApplied])
```

#### 3.4.3 Apply To State Machine
```
function applyToStateMachine(node, logEntry):
    // In a real implementation, this would modify the state machine
    // For simulation, we'll just log the application
    console.log(`Node ${node.id} applied command: ${logEntry.command}`)
```

## 4. Simulation Functions

### 4.1 Message Processing
```
function processMessages(nodes):
    for each node in nodes:
        if node.isActive:
            while node.inbox is not empty:
                message = node.inbox.dequeue()
                
                if message.type == REQUEST_VOTE:
                    handleRequestVote(node, message, nodes)
                else if message.type == REQUEST_VOTE_RESPONSE:
                    handleRequestVoteResponse(node, message, nodes)
                else if message.type == APPEND_ENTRIES:
                    handleAppendEntries(node, message, nodes)
                else if message.type == APPEND_ENTRIES_RESPONSE:
                    handleAppendEntriesResponse(node, message, nodes)
```

### 4.2 Timer Handling
```
function handleTimers(nodes, currentTime):
    for each node in nodes:
        if node.isActive:
            // Check election timeout for followers and candidates
            if (node.state == FOLLOWER || node.state == CANDIDATE) && 
               node.electionTimeout.isExpired(currentTime):
                becomeCandidate(node, nodes)
            
            // Check heartbeat timer for leaders
            if node.state == LEADER && node.heartbeatTimer.isExpired(currentTime):
                // Send heartbeats
                sendAppendEntries(node, nodes, true)
                // Reset heartbeat timer
                node.heartbeatTimer.reset(HEARTBEAT_INTERVAL)
```

### 4.3 Generate Random Log Entries
```
function generateRandomLogEntry(term):
    entry = new LogEntry()
    entry.term = term
    entry.command = randomCommandString()
    return entry
```

### 4.4 Random Command Generation
```
function randomCommandString():
    // Generate a random command (e.g., "SET key1 value2", "GET key5", etc.)
    operations = ["SET", "GET", "DELETE", "INCREMENT", "DECREMENT"]
    operation = operations[randomInt(0, operations.length - 1)]
    
    key = "key" + randomInt(1, 100)
    
    if operation == "SET":
        value = "value" + randomInt(1, 1000)
        return `${operation} ${key} ${value}`
    else:
        return `${operation} ${key}`
```

### 4.5 Simulate Node Failure
```
function crashNode(node):
    node.isActive = false
```

### 4.6 Simulate Node Recovery
```
function recoverNode(node):
    node.isActive = true
    node.state = FOLLOWER
    node.inbox = new Queue()  // Clear inbox
    
    // Don't reset persistent state (currentTerm, votedFor, log)
    
    // Reset volatile state
    node.commitIndex = 0
    node.lastApplied = 0
    
    // Restart election timeout
    node.electionTimeout.reset(randomBetween(150, 300))
```

### 4.7 Simulate Network Partition
```
function createNetworkPartition(nodes):
    // Create two partitions
    partition1Size = randomInt(1, nodes.length - 1)
    
    // Create a matrix of which nodes can communicate
    canCommunicate = []
    
    for i from 0 to nodes.length - 1:
        canCommunicate[i] = []
        for j from 0 to nodes.length - 1:
            if i < partition1Size && j < partition1Size:
                canCommunicate[i][j] = true  // Nodes in partition 1 can communicate
            else if i >= partition1Size && j >= partition1Size:
                canCommunicate[i][j] = true  // Nodes in partition 2 can communicate
            else:
                canCommunicate[i][j] = false // Nodes in different partitions cannot communicate
    
    return canCommunicate
```

### 4.8 Random Client Request
```
function simulateClientRequest(nodes):
    // Find the current leader
    leader = null
    for each node in nodes:
        if node.state == LEADER && node.isActive:
            leader = node
            break
    
    if leader == null:
        // No leader found, request fails
        return false
    
    // Generate random command
    command = randomCommandString()
    
    // Create log entry
    entry = new LogEntry()
    entry.term = leader.currentTerm
    entry.command = command
    entry.index = leader.log.length
    
    // Append to leader's log
    leader.log.push(entry)
    
    // Try to replicate to followers
    sendAppendEntries(leader, nodes, false)
    
    return true
```

## 5. Main Simulation Loop

```
function runSimulation(numNodes, steps):
    // Initialize system
    nodes = initializeSystem(numNodes)
    networkPartition = null
    currentTime = 0
    
    for step from 1 to steps:
        currentTime += 1
        
        // 1. Process messages
        processMessages(nodes)
        
        // 2. Handle timers
        handleTimers(nodes, currentTime)
        
        // 3. Randomly simulate events
        if random() < 0.01:  // 1% chance of node failure
            randomNode = nodes[randomInt(0, nodes.length - 1)]
            crashNode(randomNode)
        
        if random() < 0.01:  // 1% chance of node recovery
            inactiveNodes = findInactiveNodes(nodes)
            if inactiveNodes.length > 0:
                randomNode = inactiveNodes[randomInt(0, inactiveNodes.length - 1)]
                recoverNode(randomNode)
        
        if random() < 0.005:  // 0.5% chance of network partition
            networkPartition = createNetworkPartition(nodes)
        
        if random() < 0.01:  // 1% chance of healing network partition
            networkPartition = null
        
        if random() < 0.05:  // 5% chance of client request
            simulateClientRequest(nodes)
        
        // 4. Print state for visualization (optional)
        if step % 10 == 0:
            printSystemState(nodes)
```
