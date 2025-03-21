## 1. Node Variables and Types

| Variable | Type | Description |
|----------|------|-------------|
| `node_id` | `int` | Unique identifier for the node |
| `inbox` | `tuple` or `null` | Queue of length 1 for receiving messages |
| `voted_for` | `int` or `null` | Candidate ID this node voted for in current term (null if none) |
| `current_term` | `int` | Current term number the node is in |
| `role` | `enum {Leader, Follower, Candidate}` | Current role of the node |
| `votes_received` | `array[int]` | Array tracking votes received from each node (only relevant for candidates) |
| `timeout` | `boolean` | Whether the node has timed out |

## 2. Message Types

| Message Type | Format | Description |
|--------------|--------|-------------|
| Heartbeat | `(Hb, current_term, node_id)` | Sent by leader to maintain authority |
| Vote Request | `(Vr, current_term, node_id)` | Sent by candidate to request votes |
| Vote Grant | `(Vote, yes, node_id)` | Sent by follower granting a vote |

## 3. Initial Configuration

- One node is designated as the initial leader with `role = Leader`
- All other nodes start as followers with `role = Follower`
- All nodes begin with `current_term = 1`
- All nodes have `voted_for = null` initially
- All nodes have `votes_received = [0, 0, ..., 0]` (array of zeroes with length equal to number of nodes)
- All inboxes are empty (`inbox = null`)
- All nodes have `timeout = false`

## 4. Formal Transition Rules

### Transition 1: Follower Behavior

**Condition**: `role == Follower`

**Case 1.1**: Empty inbox
```
inbox == null → 
  timeout := true
  current_term := current_term + 1
  voted_for := null  # Reset voted_for when term changes
  voted_for := node_id  # Then vote for self
  votes_received := [0, 0, ..., 0]  # Reset vote array
  role := Candidate
  broadcast(node, (Vr, current_term, node_id))
```

**Case 1.2**: Inbox contains vote request
```
inbox == (Vr, term, sender_id) →
  timeout := false
  if term >= current_term and voted_for == null then
    send_message(node, sender_id, (Vote, yes, node_id))
    non_deterministic_change("voted_for", node_id, sender_id)  # Vote with amnesia
  endif
  inbox := null
```

**Case 1.3**: Inbox contains heartbeat with valid term
```
inbox == (Hb, term, sender_id) and term >= current_term →
  timeout := false
  if term > current_term then
    current_term := term
    voted_for := null
  endif
  inbox := null
```

**Case 1.4**: Inbox contains heartbeat with stale term
```
inbox == (Hb, term, sender_id) and term < current_term →
  timeout := true
  current_term := current_term + 1
  voted_for := null  # Reset voted_for when term changes
  role := Candidate
  voted_for := node_id  # Then vote for self
  votes_received := [0, 0, ..., 0]  # Reset vote array
  broadcast(node, (Vr, current_term, node_id))
  inbox := null
```

### Transition 2: Leader Behavior

**Condition**: `role == Leader`

**Case 2.1**: Normal operation
```
broadcast(node, (Hb, current_term, node_id))
```

**Case 2.2**: Received message with higher term
```
inbox == (Hb, term, sender_id) and term > current_term →
  current_term := term
  role := Follower
  voted_for := null
  votes_received := [0, 0, ..., 0]  # Reset vote array
  inbox := null
```

**Case 2.3**: Received vote request with higher term
```
inbox == (Vr, term, sender_id) and term > current_term →
  current_term := term
  role := Follower
  voted_for := null
  votes_received := [0, 0, ..., 0]  # Reset vote array
  inbox := null
```

### Transition 3: Candidate Behavior

**Condition**: `role == Candidate`

**Case 3.1**: Received vote
```
inbox == (Vote, yes, sender_id) →
  votes_received[sender_id] := votes_received[sender_id] + 1  # Track vote from specific node
  
  # Count total votes (including possible duplicates)
  total_votes := 0
  for each i in range(number_of_nodes):
    if votes_received[i] > 0:
      total_votes := total_votes + 1
  endfor
  
  if total_votes > number_of_nodes/2 then
    votes_received := [0, 0, ..., 0]  # Reset vote array
    role := Leader
    broadcast(node, (Hb, current_term, node_id))
  else
    broadcast(node, (Vr, current_term, node_id))
  endif
  inbox := null
```

**Case 3.2**: Received heartbeat with higher term
```
inbox == (Hb, term, sender_id) and term > current_term →
  current_term := term
  role := Follower
  voted_for := null
  votes_received := [0, 0, ..., 0]  # Reset vote array
  inbox := null
```

**Case 3.3**: Received vote request
```
inbox == (Vr, term, sender_id) →
  if term > current_term then
    current_term := term
    role := Follower
    non_deterministic_change("voted_for", node_id, sender_id)  # Vote with amnesia
    send_message(node, sender_id, (Vote, yes, node_id))
    votes_received := [0, 0, ..., 0]  # Reset vote array
  endif
  inbox := null
```

**Case 3.4**: No quorum received yet
```
if total_votes <= number_of_nodes/2 then
  broadcast(node, (Vr, current_term, node_id))
endif
```

# Protocol Functions

## Message Delivery Functions

```
function send_message(source_node, target_node, message_tuple):
  if random_bool(p_loss) then
    # Message is lost, no action taken
  else
    target_node.inbox := message_tuple
  endif
```
```
function broadcast(source_node, message_tuple):
  for each node in all_nodes:
    if node != source_node:  # Optional: don't send to self
      send_message(source_node, node, message_tuple)
    endif
  endfor
```

## State Management Functions
```
function non_deterministic_change(variable_name, node_id, new_value):
  if amnesia: 
    do nothing
  else:
    if variable_name == "node_id":
      node_id := new_value
    elif variable_name == "inbox":
      inbox := new_value
    elif variable_name == "voted_for":
      voted_for := new_value
    elif variable_name == "current_term":
      current_term := new_value
    elif variable_name == "role":
      role := new_value
    elif variable_name == "votes_received":
      votes_received := new_value
    elif variable_name == "timeout":
      timeout := new_value
```

Inbox-Constraint conditions. 

| **Role**       | **Allowed Messages** | **Sender Role** | **Term Restrictions** | **FOL Condition** |
|---------------|---------------------|----------------|----------------------|-------------------|
| **Follower**   | Heartbeat           | Leader         | Any term ≤ max(all terms) | `(Hb, sender, t) ∧ sender.role = Leader ∧ t ≤ max(Terms)` |
|               | Vote Request         | Candidate      | Any term ≤ max(all terms) | `(Vr, sender, t) ∧ sender.role = Candidate ∧ t ≤ max(Terms)` |
|               | Vote Grant           | Follower       | Term < Follower's term | `(Vote, sender, t) ∧ sender.role = Follower ∧ t < current_term` |
| **Candidate**  | Heartbeat           | Leader         | Any term ≤ max(all terms) | `(Hb, sender, t) ∧ sender.role = Leader ∧ t ≤ max(Terms)` |
|               | Vote Grant           | Follower       | Term ≤ Candidate's term | `(Vote, sender, t) ∧ sender.role = Follower ∧ t ≤ current_term` |
|               | Vote Request         | Candidate      | Any term ≤ max(all terms) | `(Vr, sender, t) ∧ sender.role = Candidate ∧ t ≤ max(Terms)` |
| **Leader**     | Vote Grant          | Follower       | Term ≤ Leader’s term | `(Vote, sender, t) ∧ sender.role = Follower ∧ t ≤ current_term` |
|               | Vote Request         | Candidate      | Any term ≤ max(all terms) | `(Vr, sender, t) ∧ sender.role = Candidate ∧ t ≤ max(Terms)` |
|               | Heartbeat            | Leader         | Any term ≤ max(all terms) | `(Hb, sender, t) ∧ sender.role = Leader ∧ t ≤ max(Terms)` |





## Notes

- Whatever changes we want to be non-deterministic we can do through the non-deterministic_change function. This makes it easier to make things deterministic and non deterministic. 
- What to do when leader recieves a same term vote request? Is it even possible. 
-  When a follower receives a heartbeat from a leader or a vote request from a candidate with a higher term, should it update its `current_term` immediately? I am doing that rn in this. 
- Its good to leave the inbox a little liberal for experimentation at start ig. 
- currently i havent removed the send message feature. The idea is to have an inbox with two cells, one with non determinsitic message constrained by the properties in the inbox-constrains table and second is the explicit messages sent by other nodes. A node can non-deter choose b/w two while reading inbox.

- A follower should reset its timeout on receiving a valid heartbeat.
- Currently, `timeout := false` is correctly set in Case 1.3, but the timeout logic should be explicitly reset to prevent unnecessary elections.

- If a follower receives a heartbeat with an outdated term, it currently assumes leadership failure and starts a new election. Is that viable since it could be a Hb from a leader from one of past terms. 

- If a leader or candidate receives a message with a higher term:
  - Explicitly reset state variables, including:
    ```
    current_term := term
    role := Follower
    voted_for := null
    votes_received := [0, 0, ..., 0]  
    inbox := null
    ```


## Note 2 
-  We discussed in the meeting that when a follower sees a vote request but its votedFor is already something else in this node, it ignores the vote request. But when it sees the vote request again from the same node it has voted for, it sends the vote again? Then do inject the bug instead of counting multiple votes from the nodes as one vote, we just count all the votes. I believe this implmentation is wrong. 
-Ideally what should happen is that after the votedFor has been set, no other votes can be sent from that node again. The only reason somehow a node sends vote again, is when it either forgets that it already voted, or when it thinks that the past vote didnt reach the reciever (according to the fuzzing raft paper). In this case, from the followers perspective its still sending only one valid legal vote. 
- In this code I have implemented the latter one but the way I am making a node send two different votes is by making votedFor change non-deterministic and only having a single `int` variable tracking the number of votes a node got instead of an `arr` . However this doesnt just take care of dduplication bug, this also simulates the bug in which a node sends votes to two different nodes. Is this a problem? 
