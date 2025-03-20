# Leader Election Protocol Specification with Message Inbox

### **Node Parameters with Data Types**
Each node has the following parameters:

| **Parameter**       | **Data Type**                | **Description**                                                                 |
|---------------------|------------------------------|---------------------------------------------------------------------------------|
| `current_term`      | `int`                        | Term of the current leader election protocol.                                   |
| `node_id`           | `int`                        | Unique ID of the node.                                                          |
| `voted_for`         | `int` or `null`              | ID of the node it last voted for (or `null` if it hasn't voted).                |
| `timeout`           | `bool`                       | Boolean that indicates if the node has timed out.                               |
| `lfc`               | `list[bool]` (size 3)        | 3-bit vector indicating the node's state (`Leader`, `Follower`, `Candidate`).   |
| `rec_vote`          | `int`                        | Number of votes received (only relevant for candidates).                        |
| `rec_heart`         | `int`                        | ID of the leader who sent the last heartbeat (or `0` if no heartbeat received). |
| `rec_heart_term`    | `int`                        | Term of the leader who sent the last heartbeat.                                 |
| `node_active`       | `bool`                       | Random boolean that sets the node active or inactive (simulates crashes).       |
| `vote_inbox`        | `list[tuple(int, int)]`      | Inbox containing vote requests as (candidate_id, term) tuples.                  |

---

### **Initial Configuration**
- One node is the leader (`lfc = [1, 0, 0]`), and all others are followers (`lfc = [0, 1, 0]`).
- All nodes have `voted_for` set to the leader's `node_id`.
- The leader's `rec_heart` is set to its own `node_id`, and followers' `rec_heart` is set to the leader's `node_id`.
- All `vote_inbox` lists start empty.

---

### **Nondeterministic Transitions**

#### **T1: Leader Sends Heartbeats**
- **Condition**: Leader is active (`node_active = true`).
- **Action**:
  1. Leader sets `rec_heart` of all active followers to its `node_id`.
  2. Leader sets `rec_heart_term` of all active followers to its `current_term`.
- **Effect**:
  - Followers reset their `timeout` to `false`.
  - Followers recognize the leader as valid.

---

#### **T2: Follower Checks Heartbeat**
- **Condition**: Follower is active (`node_active = true`).
- **Action**:
  1. If `rec_heart_term < current_term`, follower becomes a candidate (`lfc = [0, 0, 1]`).
  2. If `rec_heart == 0`, follower sets `timeout = true`.
- **Effect**:
  - Follower may start a new election if no heartbeat is received.

---

#### **T3: Follower Times Out**
- **Condition**: Follower's `timeout` is `true`.
- **Action**:
  1. Follower becomes a candidate (`lfc = [0, 0, 1]`).
  2. Increments `current_term` by 1.
  3. Sets `voted_for` to its own `node_id`.
  4. Prepares to send `RequestVote` to all active nodes.
- **Effect**:
  - Candidate starts a new election.

---

#### **T4: Candidate Requests Votes**
- **Condition**: Candidate is active (`node_active = true`).
- **Action**:
  1. Adds a tuple `(node_id, current_term)` to the `vote_inbox` of each active node.
- **Effect**:
  - Vote requests are queued in other nodes' inboxes for processing.

---

#### **T5: Follower Processes Vote Request**
- **Condition**: Follower has a non-empty `vote_inbox`.
- **Action**:
  1. Follower reads the next `(candidate_id, term)` tuple from its `vote_inbox`.
  2. If `term >= current_term` and (`voted_for` is null or equals `candidate_id`), follower grants the vote.
  3. Sets `voted_for` to the candidate's `node_id`.
  4. Removes the processed request from `vote_inbox`.
- **Effect**:
  - Candidate receives a vote if the request is valid.
  - The vote request is removed from the inbox after processing.

---

#### **T6: Candidate Wins Election**
- **Condition**: Candidate's `rec_vote > n/2`.
- **Action**:
  1. Candidate becomes leader (`lfc = [1, 0, 0]`).
  2. Resets `rec_vote` to `0`.
  3. Starts sending heartbeats.
- **Effect**:
  - New leader is elected.

---

#### **T7: Term Update on Higher Term RPC**
- **Condition**: Node processes a vote request from its `vote_inbox` with a higher term.
- **Action**:
  1. Updates its `current_term` to the received term.
  2. Reverts to a follower (`lfc = [0, 1, 0]`).
  3. Resets `voted_for` to `null`.
- **Effect**:
  - Ensures nodes recognize the most up-to-date leader.

---

### **How Each Transition Works**

1. **Leader Sends Heartbeats**:
   - The leader periodically sends heartbeats to all followers to maintain its authority.
   - Followers reset their `timeout` upon receiving a valid heartbeat.

2. **Follower Checks Heartbeat**:
   - Followers verify if the heartbeat is from a valid leader (i.e., `rec_heart_term >= current_term`).
   - If no heartbeat is received (`rec_heart == 0`), the follower starts a new election.

3. **Follower Times Out**:
   - If a follower's `timeout` becomes `true`, it transitions to a candidate and starts a new election.

4. **Candidate Requests Votes**:
   - The candidate adds vote request tuples `(node_id, current_term)` to the inboxes of other nodes.
   - This models asynchronous message passing in a distributed system.

5. **Follower Processes Vote Request**:
   - Followers process vote requests from their inbox one at a time.
   - Each request contains the candidate's ID and term, which the follower evaluates.
   - Followers grant votes to candidates with a higher or equal term if they haven't voted already.

6. **Candidate Wins Election**:
   - If a candidate receives votes from a majority of nodes, it becomes the leader.

7. **Term Update on Higher Term RPC**:
   - If a node receives a vote request with a higher term, it updates its term and reverts to a follower.

---

### **Message Inbox Design**

The `vote_inbox` represents a queue of pending vote requests, which has several advantages:

1. **Realistic Message Passing**: Models the asynchronous message-passing nature of distributed systems.
2. **Race Conditions**: Captures scenarios where multiple candidates might request votes simultaneously.
3. **Message Processing Order**: Allows nodes to process requests in arbitrary order, mimicking real network behavior.
4. **Message Buffering**: Represents network delays where messages might be received but not immediately processed.

Each vote request in the inbox is a tuple containing:
- The requesting candidate's node ID
- The candidate's current term

Nodes process these requests one at a time, and once processed, the request is removed from the inbox.

---

### **Safety Properties**

1. **No Two Leaders in Same Term**: At most one leader can exist in a given term.
2. **Terms Never Decrease**: A node's term can only increase, never decrease.

---

### **Liveness Properties**

1. **Leader Election**: Eventually, a leader will be elected if enough nodes are active.

---

### **Summary**
This model incorporates a message inbox for handling vote requests, making it more realistic by capturing asynchronous message passing. It ensures that the leader election process aligns with Raft's specifications, including handling **stale leaders**, **timeouts**, **term updates**, and proper **message handling**.
