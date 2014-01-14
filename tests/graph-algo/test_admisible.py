'''
Created on November 27, 2013

@author: aousterh
'''
import random
import sys
import timeit
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import structures
import admissible
import fpring
from timing_util import *

class Test(unittest.TestCase):
    
    def test_one_request(self):
        """Basic test involving one src/dst pair."""

        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(False, 0, 0, 2, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        # Make a request
        admissible.add_backlog(status, 0, 1, 5)
 
        # Get admissible traffic (timeslot 1)
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted in each of the first 5 tslots
        # and queue_0 and queue_1 are empty
        for i in range(5):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
            self.assertEqual(admitted_i.edges.src, 0)
            self.assertEqual(admitted_i.edges.dst, 1)
        for i in range(5, structures.BATCH_SIZE):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass

    def test_multiple_competing_requests(self):
        """Test of two flows from the same source. """
 
        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(False, 0, 0, 5, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        # Make a few competing requests
        admissible.add_backlog(status, 0, 1, 2)
        admissible.add_backlog(status, 0, 4, 1)
 
        # Get admissible traffic, check that one packet was admitted
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted in each of first 3 timeslots
        for i in range(3):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
 
        # Check that no packets were admitted in remaining timeslots
        for i in range(3, structures.BATCH_SIZE):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass

    def test_out_of_order_requests(self):
        """Tests that requests are admitted in the order of last send timeslot,
        regardless of the order the requests arrive in."""

        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(False, 0, 0, 6, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        # Make two competing requests
        admissible.add_backlog(status, 3, 5, 1)
        admissible.add_backlog(status, 4, 5, 1)
 
        # Get admissible traffic (first batch)
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted from src 3 in timeslot 0
        admitted_0 = admissible.dequeue_admitted_traffic(status)
        self.assertEqual(admitted_0.size, 1)
        self.assertEqual(admitted_0.edges.src, 3)

        # Check that one packet was admitted from src 4 in timeslot 1
        admitted_1 = admissible.dequeue_admitted_traffic(status)
        self.assertEqual(admitted_1.size, 1)
        self.assertEqual(admitted_1.edges.src, 4)

        # Check that no more packets were admitted in this batch
        for i in range(2, structures.BATCH_SIZE):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # Make two competing requests out of their timeslot order
        admissible.add_backlog(status, 4, 5, 2)
        admissible.add_backlog(status, 3, 5, 2)

        # Get admissible traffic (second batch)
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted from src 3 in timeslot 0
        admitted_0 = admissible.dequeue_admitted_traffic(status)
        self.assertEqual(admitted_0.size, 1)
        self.assertEqual(admitted_0.edges.src, 3)

        # Check that one packet was admitted from src 4 in timeslot 1
        admitted_1 = admissible.dequeue_admitted_traffic(status)
        self.assertEqual(admitted_1.size, 1)
        self.assertEqual(admitted_1.edges.src, 4)

        # should clean up memory

        pass

    def test_many_requests(self):
        """Tests the admissible algorithm over a long time, including oversubscription."""
  
        n_nodes = 64
        max_r_per_t = 10  # max requests per timeslot
        duration = 100000
        max_size = 20
        rack_capacity = 24

        # Track pending requests - mapping from src/dst to num requested
        pending_requests = {}
        # Track total demands
        cumulative_demands = {}

        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(True, rack_capacity, 0, n_nodes, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        num_admitted = 0
        num_requested = 0
        for b in range(duration / structures.BATCH_SIZE):
            # Make some new requests
            for t in range(structures.BATCH_SIZE):
                requests_per_timeslot = random.randint(0, max_r_per_t)
                for r in range(requests_per_timeslot):
                    src = random.randint(0, n_nodes-1)
                    dst = random.randint(0, n_nodes-2)
                    if (dst >= src):
                        dst += 1 # don't send to self
                    size = random.randint(1, max_size)
                    demand = cumulative_demands.get((src, dst), 0)
                    demand += size
                    cumulative_demands[(src, dst)] = demand
                    if (src, dst) in pending_requests.keys():
                        pending_requests[(src, dst)] = pending_requests[(src, dst)] + size
                    else:
                        pending_requests[(src, dst)] = size
                    admissible.add_backlog(status, src, dst, size)
                    num_requested += size
            
            # Get admissible traffic for this batch
            admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
            
            for i in range(structures.BATCH_SIZE):
                admitted_i = admissible.dequeue_admitted_traffic(status)
                num_admitted += admitted_i.size

                # Check all admitted edges - make sure they were requested
                # and have not yet been fulfilled
                self.assertTrue(admitted_i.size <= n_nodes)
                rack_outputs = [0, 0]
                rack_inputs = [0, 0]
                for e in range(admitted_i.size):
                    edge = structures.get_admitted_edge(admitted_i, e)
                    pending_count = pending_requests[(edge.src, edge.dst)]
                    self.assertTrue(pending_count >= 1)
                    if pending_count > 1:
                        pending_requests[(edge.src, edge.dst)] = pending_count - 1
                    else:
                        del pending_requests[(edge.src, edge.dst)]

                    rack_outputs[structures.get_rack_from_id(edge.src)] += 1
                    rack_inputs[structures.get_rack_from_id(edge.dst)] += 1
            
                for index in range(len(rack_outputs)):
                    self.assertTrue(rack_outputs[index] <= rack_capacity)
                    self.assertTrue(rack_inputs[index] <= rack_capacity)
                
        print 'requested %d, admitted %d, capacity %d' % (num_requested, num_admitted, duration * n_nodes)

        # should clean up memory

        pass

    def test_oversubscribed(self):
        """Tests networks which are oversubscribed on the uplinks from racks/downlinks to racks."""

        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(True, 2, 0, 128, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        # Make requests that could overfill the links above the ToRs
        admissible.add_backlog(status, 0, 32, 1)
        admissible.add_backlog(status, 1, 64, 1)
        admissible.add_backlog(status, 2, 96, 1)
        admissible.add_backlog(status, 33, 65, 1)
        admissible.add_backlog(status, 97, 66, 1)
    
        # Get admissible traffic
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
   
        # Check that we admitted at most 2 packets for each of the
        # oversubscribed links
        admitted = admissible.dequeue_admitted_traffic(status)
        rack_0_out = 0
        rack_2_in = 0
        for e in range(admitted.size):
            edge = structures.get_admitted_edge(admitted, e)
            if structures.get_rack_from_id(edge.src) == 0:
                rack_0_out += 1
            if structures.get_rack_from_id(edge.dst) == 2:
                rack_2_in += 1

        self.assertEqual(rack_0_out, 2)
        self.assertEqual(rack_2_in, 2)

        # should clean up memory

        pass

    def test_reset_sender(self):
        '''Tests resetting a sender.'''

        # initialization
        q_bin = fpring.fp_ring_create(structures.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structures.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structures.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structures.BATCH_SHIFT)
        core = structures.create_admission_core_state()
        structures.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structures.create_admissible_status(False, 0, 0, 21, q_head,
                                                     q_admitted_out)
        admitted_batch = structures.create_admitted_batch()

        for i in range(0, structures.NUM_BINS):
            empty_bin = structures.create_bin(structures.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissible.enqueue_head_token(q_urgent)

        # Make requests
        admissible.add_backlog(status, 0, 10, structures.BATCH_SIZE)
        admissible.add_backlog(status, 1, 10, structures.BATCH_SIZE)
        admissible.add_backlog(status, 0, 20, structures.BATCH_SIZE)

        # Get admissible traffic
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
   
        # Check admitted traffic
        for i in range(structures.BATCH_SIZE):
            admitted_i = admissible.dequeue_admitted_traffic(status)

            if i % 2 == 0:
                self.assertEqual(admitted_i.size, 1)
                edge = structures.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge.src, 0)
                self.assertEqual(edge.dst, 10)
            else:
                self.assertEqual(admitted_i.size, 2)
                edge_0 = structures.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge_0.src, 1)
                self.assertEqual(edge_0.dst, 10)
                edge_1 = structures.get_admitted_edge(admitted_i, 1)
                self.assertEqual(edge_1.src, 0)
                self.assertEqual(edge_1.dst, 20)

        # Reset src 0
        admissible.reset_sender(status, 0)

        # Get admissible traffic again
        admissible.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
   
        # Check that we admit only one more packet for each of src 0's
        # pending flows
        for i in range(structures.BATCH_SIZE):
            admitted_i = admissible.dequeue_admitted_traffic(status)
            
            if i == 0:
                self.assertEqual(admitted_i.size, 1)
                edge = structures.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge.src, 0)
                self.assertEqual(edge.dst, 10)
            elif i == 1:
                self.assertEqual(admitted_i.size, 2)
                edge_0 = structures.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge_0.src, 1)
                self.assertEqual(edge_0.dst, 10)
                edge_1 = structures.get_admitted_edge(admitted_i, 1)
                self.assertEqual(edge_1.src, 0)
                self.assertEqual(edge_1.dst, 20)
            elif i < structures.BATCH_SIZE / 2 + 1:
                self.assertEqual(admitted_i.size, 1)
                edge = structures.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge.src, 1)
                self.assertEqual(edge.dst, 10)
            else:
                self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass
      
if __name__ == "__main__":
    unittest.main()
