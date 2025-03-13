### **Node Parameters with Data Types**
Each node has the following parameters:

| **Parameter**       | **Data Type**       | **Description**                                                                 |
|---------------------|---------------------|---------------------------------------------------------------------------------|
| `current_term`      | `int`               | Term of the current leader election protocol.                                   |
| `node_id`           | `int`               | Unique ID of the node.                                                          |
| `voted_for`         | `int` or `null`     | ID of the node it last voted for (or `null` if it hasn’t voted).                |
| `timeout`           | `bool`              | Boolean that indicates if the node has timed out.                               |
| `lfc`               | `list[bool]` (size 3)| 3-bit vector indicating the node’s state (`Leader`, `Follower`, `Candidate`).   |
| `rec_vote`          | `int`               | Number of votes received (only relevant for candidates).                        |
| `rec_heart`         | `int`               | ID of the leader who sent the last heartbeat (or `0` if no heartbeat received). |
| `rec_heart_term`    | `int`               | Term of the leader who sent the last heartbeat.                                 |
| `node_active`       | `bool`              | Random boolean that sets the node active or inactive (simulates crashes).       |
| `asking_vote`       | `int`               | ID of the candidate who requested a vote (or `0` if no request).                |
| `asking_term`       | `int`               | Term of the candidate who requested a vote (or `0` if no request).              |

instead of asking vote and asking term theres an array, which takes a tuple (asking vote node, asking vote node term), in which a node can append thier vote request. A node will read the 
---

### **Initial Configuration**
- One node is the leader (`lfc = [1, 0, 0]`), and all others are followers (`lfc = [0, 1, 0]`).
- All nodes have `voted_for` set to the leader’s `node_id`.
- The leader’s `rec_heart` is set to its own `node_id`, and followers’ `rec_heart` is set to the leader’s `node_id`.

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
- **Condition**: Follower’s `timeout` is `true`.
- **Action**:
  1. Follower becomes a candidate (`lfc = [0, 0, 1]`).
  2. Increments `current_term` by 1.
  3. Sets `voted_for` to its own `node_id`.
  4. Sends `RequestVote` to all active nodes.
- **Effect**:
  - Candidate starts a new election.

---

#### **T4: Candidate Requests Votes**
- **Condition**: Candidate is active (`node_active = true`).
- **Action**:
  1. Sets `asking_vote` to its `node_id` and `asking_term` to its `current_term`.
  2. Sends `RequestVote` to all active nodes.
- **Effect**:
  - Nodes process the vote request.

---

#### **T5: Follower Grants Vote**
- **Condition**: Follower receives `RequestVote`.
- **Action**:
  1. If `asking_term >= current_term`, follower grants the vote.
  2. Sets `voted_for` to the candidate’s `node_id`.
  3. Increments the candidate’s `rec_vote`.
- **Effect**:
  - Candidate may win the election if it receives a majority of votes.

---

#### **T6: Candidate Wins Election**
- **Condition**: Candidate’s `rec_vote > n/2`.
- **Action**:
  1. Candidate becomes leader (`lfc = [1, 0, 0]`).
  2. Resets `rec_vote` to `0`.
  3. Starts sending heartbeats.
- **Effect**:
  - New leader is elected.

---

#### **T7: Term Update on Higher Term RPC**
- **Condition**: Node receives an RPC (`RequestVote` or `AppendEntries`) with a higher term.
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
   - If a follower’s `timeout` becomes `true`, it transitions to a candidate and starts a new election.

4. **Candidate Requests Votes**:
   - The candidate requests votes from all active nodes, providing its `node_id` and `current_term`.

5. **Follower Grants Vote**:
   - Followers grant votes to candidates with a higher or equal term (`asking_term >= current_term`).

6. **Candidate Wins Election**:
   - If a candidate receives votes from a majority of nodes, it becomes the leader.

7. **Term Update on Higher Term RPC**:
   - If a node receives an RPC with a higher term, it updates its `current_term` and reverts to a follower.

---

### **Summary**
This model includes **data types for all variables** and a detailed explanation of **how each transition works**. It ensures that the leader election process aligns with Raft’s specifications, including handling **stale leaders**, **timeouts**, and **term updates**. Let me know if you need further clarification!
