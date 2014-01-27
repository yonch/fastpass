'''
Created on January 19, 2014

@author: aousterh
'''
import random
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')

import structures
import structuressjf
import admissible
import admissiblesjf
import fpring
import genrequests

class pending_request(object):
    '''Tracks info about a pending request in an admissible experiment.'''

    def __init__(self, size, request_time):
        self.size = size
        self.request_time = request_time

class admissible_runner(object):
    '''Runs an admissible traffic experiment.'''

    ROUND_ROBIN = 0
    SHORTEST_JOB_FIRST = 1
    RANDOM = 2

    def __init__(self, num_nodes, duration, warm_up_duration, request_size):
        self.num_nodes = num_nodes
        self.requests = []
        self.duration = duration  # total duration, including warm-up
        self.warm_up_duration = warm_up_duration
        self.request_size = request_size

    def generate_request_stream(self, mean_t_btwn_requests):
        generator = genrequests.create_request_generator(mean_t_btwn_requests,
                                                         0, self.num_nodes)

        # Free any requests generated before
        for r in self.requests:
            genrequests.destroy_request(r)
        self.requests = []

        current_time = 0
        while current_time < self.duration:
            req = genrequests.create_next_request(generator)
            self.requests.append(req)
            current_time = req.time

    def run_round_robin_admissible(self):
        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.FP_NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.FP_NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)

        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(False, 0, 0, self.num_nodes,
                                                     q_head, q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        num_admitted = 0
        num_requested = 0
        # TODO: can we run this so that request arrivals are inter-leaved with
        # getting admissible traffic? would be more realistic.
        current_request = 0
        req = self.requests[current_request]
        for t in range(self.duration):
            # Issue new requests
            while int(req.time) == t:
                num_requested += self.request_size
                admissible.add_backlog(status, req.src, req.dst, self.request_size)
                self.pending_requests[(req.src, req.dst)].append(pending_request(self.request_size, t))
                current_request += 1
   
                req = self.requests[current_request]
                
            if t % structures.BATCH_SIZE != structures.BATCH_SIZE - 1:
                continue

            # Get admissible traffic for this batch
            admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
            
            if t < self.warm_up_duration:
                continue

            # Record stats
            for i in range(structures.BATCH_SIZE):
                admitted_i = admissible.dequeue_admitted_traffic(status)
                num_admitted += admitted_i.size

                for e in range(admitted_i.size):
                    edge = structures.get_admitted_edge(admitted_i, e)
                    req_list = self.pending_requests[(edge.src, edge.dst)]
                    if len(req_list) < 1:
                        raise AssertionError
                    req_list[0].size -= 1
                    if req_list[0].size == 0:
                        # record flow completion time
                        last_t_slot = t + i
                        fct = last_t_slot - req_list[0].request_time
                        self.flow_completion_times.append(fct)
                        del req_list[0]
                
        capacity = (self.duration - self.warm_up_duration) * self.num_nodes
        observed_util = float(num_admitted) / capacity
                                                               
        # should clean up memory

        return observed_util

    def run_shortest_job_first_admissible(self):
        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.FP_NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.FP_NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)

        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(False, 0, 0, self.num_nodes,
                                                     q_head, q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        num_admitted = 0
        num_requested = 0
        # TODO: can we run this so that request arrivals are inter-leaved with
        # getting admissible traffic? would be more realistic.
        current_request = 0
        req = self.requests[current_request]
        for t in range(self.duration):
            # Issue new requests
            while int(req.time) == t:
                num_requested += self.request_size
                admissiblesjf.add_backlog(status, req.src, req.dst, self.request_size)
                self.pending_requests[(req.src, req.dst)].append(pending_request(self.request_size, t))
                current_request += 1
   
                req = self.requests[current_request]
                
            if t % structuressjf.BATCH_SIZE != structuressjf.BATCH_SIZE - 1:
                continue

            # Get admissible traffic for this batch
            admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
            
            if t < self.warm_up_duration:
                continue

            # Record stats
            for i in range(structuressjf.BATCH_SIZE):
                admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
                num_admitted += admitted_i.size

                for e in range(admitted_i.size):
                    edge = structuressjf.get_admitted_edge(admitted_i, e)
                    req_list = self.pending_requests[(edge.src, edge.dst)]
                    if len(req_list) < 1:
                        raise AssertionError
                    req_list[0].size -= 1
                    if req_list[0].size == 0:
                        # record flow completion time
                        last_t_slot = t + i
                        fct = last_t_slot - req_list[0].request_time
                        self.flow_completion_times.append(fct)
                        del req_list[0]
                
        capacity = (self.duration - self.warm_up_duration) * self.num_nodes
        observed_util = float(num_admitted) / capacity
                                                               
        # should clean up memory

        return observed_util

    def run_random_admissible(self):
        BATCH_SIZE = structures.BATCH_SIZE
        total_backlog = {}

        num_admitted = 0
        num_requested = 0
        current_request = 0
        req = self.requests[current_request]
        for t in range(self.duration):
            # Issue new requests
            while int(req.time) == t:
                num_requested += self.request_size
                total_backlog[(req.src, req.dst)] = total_backlog.get((req.src, req.dst),
                                                                      0) + self.request_size
                self.pending_requests[(req.src, req.dst)].append(pending_request(self.request_size, t))
                current_request += 1
                req = self.requests[current_request]

            if t % BATCH_SIZE != BATCH_SIZE - 1:
                continue

            # Get admissible traffic for this batch by choosing randomly from amongst
            # all flows with pending backlog
            admitted_batch = []
            for i in range(BATCH_SIZE):
                admitted_batch.append(set())
            next_backlog = {}
            for i in range(BATCH_SIZE):
                srcs = set()
                dsts = set()
                while len(admitted_batch[i]) < self.num_nodes and len(total_backlog) > 0:
                    index = random.randint(0, len(total_backlog) - 1)
                    backlog_record = total_backlog.items()[index]
                    pair = backlog_record[0]
                    backlog = backlog_record[1]
                    if pair[0] not in srcs and pair[1] not in dsts:
                        # admit
                        srcs.add(pair[0])
                        dsts.add(pair[1])
                        admitted_batch[i].add(pair)
                        backlog -= 1
                    
                    # move backlog to be considered in the next timeslot
                    if backlog > 0:
                        next_backlog[pair] = backlog
                    del total_backlog[pair]
                total_backlog = dict(total_backlog.items() + next_backlog.items())
                next_backlog = {}
                
            if t < self.warm_up_duration:
                continue

            # Record stats
            for i in range(BATCH_SIZE):
                admitted_i = admitted_batch[i]
                num_admitted += len(admitted_i)
                if len(admitted_i) > self.num_nodes:
                    raise AssertionError

                for e in range(len(admitted_i)):
                    edge = admitted_i.pop()
                    req_list = self.pending_requests[(edge[0], edge[1])]
                    if len(req_list) < 1:
                        raise AssertionError
                    req_list[0].size -= 1
                    if req_list[0].size == 0:
                        # record flow completion time
                        last_t_slot = t + i
                        fct = last_t_slot - req_list[0].request_time
                        self.flow_completion_times.append(fct)
                        del req_list[0]
                
        capacity = (self.duration - self.warm_up_duration) * self.num_nodes
        observed_util = float(num_admitted) / capacity

        return observed_util        

    def run_admissible(self, admissible_type):
        '''Run an admissible algorithm of a specific type. 0=round-robin, 1=sjf, 2=random.'''

        # Track pending requests - mapping from (src, dst) to list of requests
        self.pending_requests = {}
        for src in range(self.num_nodes):
            for dst in range(self.num_nodes):
                if src == dst:
                    continue
                self.pending_requests[(src, dst)] = []
        self.flow_completion_times = []

        if admissible_type == self.ROUND_ROBIN:
            return self.run_round_robin_admissible()
        elif admissible_type == self.SHORTEST_JOB_FIRST:
            return self.run_shortest_job_first_admissible()
        elif admissible_type == self.RANDOM:
            return self.run_random_admissible()

class Test(unittest.TestCase):

    def test_benchmark(self):
        """Benchmarks the flow completion time of different admissible traffic algorithms."""

        # too many fractions takes too long and yields a crowded plot!
        fractions = [0.2, 0.5, 0.9, 0.99]
        algorithms =  [(admissible_runner.ROUND_ROBIN, "round_robin"),
                       (admissible_runner.SHORTEST_JOB_FIRST, "shortest_job_first"),
                       (admissible_runner.RANDOM, "random")]
        num_nodes = 256
        # keep both durations an even number of batches so that bin pointers return to queue_0
        warm_up_duration = ((10000 + 127) / 128) * 128
        duration = warm_up_duration + ((50000 + 127) / 128) * 128
        request_mtus = 100 * 1000 / 1500  # 100 KB

        runner = admissible_runner(num_nodes, duration, warm_up_duration, request_mtus)

        print 'algo, target_util, fct, observed_util'
        for fraction in fractions:
            # Generate a sequence of requests for this network utilization
            mean_t = request_mtus / fraction
            runner.generate_request_stream(mean_t)
            
            for algo in algorithms:
                # For each algorithm, run the algorithm with the sequence of requests
                observed_util = runner.run_admissible(algo[0])

                # Print out results
                for fct in runner.flow_completion_times:
                    s = algo[1] + ', ' + str(fraction) + ', ' + str(fct) + ', ' + str(observed_util)
                    print(s)

if __name__ == "__main__":
    unittest.main()
