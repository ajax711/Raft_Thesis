## 1. Node Variables and Types

| Variable | Type | Description |
|----------|------|-------------|
| `node_id` | `int` | Unique identifier for the node |
| `inbox` | `tuple` or `null` | Queue of length 1 for receiving messages |
| `voted_for` | `int` or `null` | Candidate ID this node voted for in current term (null if none) |
| `current_term` | `int` | Current term number the node is in |
| `role` | `enum {Leader, Follower, Candidate}` | Current role of the node |
| `votes_received` | `int` | Count of votes received (only relevant for candidates) |
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
- All nodes have `votes_received = 0`
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
  votes_received := 1  # Count own vote
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
  votes_received := 1  # Count own vote
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
  votes_received := 0  # Reset vote count
  inbox := null
```

**Case 2.3**: Received vote request with higher term
```
inbox == (Vr, term, sender_id) and term > current_term →
  current_term := term
  role := Follower
  voted_for := null
  votes_received := 0  # Reset vote count
  inbox := null
```

### Transition 3: Candidate Behavior

**Condition**: `role == Candidate`

**Case 3.1**: Received vote
```
inbox == (Vote, yes, sender_id) →
  votes_received := votes_received + 1
  if votes_received > number_of_nodes/2 then
    votes_received := 0  # Reset vote count
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
  votes_received := 0  # Reset vote count
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
    votes_received := 0  # Reset vote count
  endif
  inbox := null
```

**Case 3.4**: No quorum received yet
```
if votes_received <= number_of_nodes/2 then
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

## Notes

- Whatever changes we want to be non-deterministic we can do through the non-deterministic_change function 
- I still havent added the node_active boolean, however i believe it is still covered if the non_determism is applied correctly. I believe it might need additions right now. 
-  When a follower receives a heartbeat from a leader or a vote request from a candidate with a higher term, should it update its `current_term` immediately? I am doing that rn in this. 
-  We discussed in the meeting that when a follower sees a vote request but its votedFor is already something else in this node, it ignores the vote request. But when it sees the vote request again from the same node it has voted for, it sends the vote again? Then do inject the bug instead of counting multiple votes from the nodes as one vote, we just count all the votes. I believe this implmentation is wrong. 
-Ideally what should happen is that after the votedFor has been set, no other votes can be sent from that node again. The only reason somehow a node sends vote again, is when it either forgets that it already voted, or when it thinks that the past vote didnt reach the reciever (according to the fuzzing raft paper). In this case, from the followers perspective its still sending only one valid legal vote. 
- In this code I have implemented the latter one but the way I am making a node send two different votes is by making votedFor change non-deterministic. However this doesnt just take care of dduplication bug, this also simulates the bug in which a node sends votes to two different nodes. 
