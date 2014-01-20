'''
Created on January 15, 2014

@author: aousterh
'''
import random
import sys
import timeit
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import structuressjf
import admissiblesjf
import fpring
from timing_util import *

class Test(unittest.TestCase):
    
    def test_one_request(self):
        """Basic test involving one src/dst pair."""

        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(False, 0, 0, 2, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        # Make a request
        admissiblesjf.add_backlog(status, 0, 1, 5)
 
        # Get admissible traffic (timeslot 1)
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted in each of the first 5 tslots
        # and queue_0 and queue_1 are empty
        for i in range(5):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
            self.assertEqual(admitted_i.edges.src, 0)
            self.assertEqual(admitted_i.edges.dst, 1)
        for i in range(5, structuressjf.BATCH_SIZE):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass

    def test_multiple_competing_requests(self):
        """Test of two flows from the same source. """
 
        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(False, 0, 0, 5, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        # Make a few competing requests
        admissiblesjf.add_backlog(status, 0, 1, 2)
        admissiblesjf.add_backlog(status, 0, 4, 1)
 
        # Get admissible traffic, check that one packet was admitted
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted in each of first 3 timeslots
        # Check that the shorter job was scheduled first
        for i in range(3):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
            if i == 0:
                edge = structuressjf.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge.src, 0)
                self.assertEqual(edge.dst, 4)
            else:
                edge = structuressjf.get_admitted_edge(admitted_i, 0)
                self.assertEqual(edge.src, 0)
                self.assertEqual(edge.dst, 1)
                
        # Check that no packets were admitted in remaining timeslots
        for i in range(3, structuressjf.BATCH_SIZE):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass

    def test_shortest_remaining_job(self):
        """Tests that requests are admitted in the order of shortest remaining."""

        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(False, 0, 0, 6, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        # Make one request for a large job which will overflow a batch
        admissiblesjf.add_backlog(status, 3, 5, 100)
 
        # Get admissible traffic (first batch)
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted per timeslot
        for i in range(structuressjf.BATCH_SIZE):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
            for e in range(admitted_i.size):
                edge = structuressjf.get_admitted_edge(admitted_i, e)
                self.assertEqual(edge.src, 3)
                self.assertEqual(edge.dst, 5)

        # Make competing requests for smaller jobs
        admissiblesjf.add_backlog(status, 3, 2, 8)
        admissiblesjf.add_backlog(status, 3, 4, 20)

        # Get admissible traffic (second batch)
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)

        # Check that one packet was admitted per timeslot and that shorter jobs
        # were completed first
        for i in range(structuressjf.BATCH_SIZE):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 1)
            for e in range(admitted_i.size):
                edge = structuressjf.get_admitted_edge(admitted_i, e)
                self.assertEqual(edge.src, 3)
                if i in range(8):
                    self.assertEqual(edge.dst, 2)
                elif i in range(8, 28):
                    self.assertEqual(edge.dst, 4)
                else:
                    self.assertEqual(edge.dst, 5)

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
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(True, rack_capacity, 0, n_nodes, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        num_admitted = 0
        num_requested = 0
        for b in range(duration / structuressjf.BATCH_SIZE):
            # Make some new requests
            for t in range(structuressjf.BATCH_SIZE):
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
                    admissiblesjf.add_backlog(status, src, dst, size)
                    num_requested += size
            
            # Get admissible traffic for this batch
            admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
            
            for i in range(structuressjf.BATCH_SIZE):
                admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
                num_admitted += admitted_i.size

                # Check all admitted edges - make sure they were requested
                # and have not yet been fulfilled
                self.assertTrue(admitted_i.size <= n_nodes)
                rack_outputs = [0, 0]
                rack_inputs = [0, 0]
                for e in range(admitted_i.size):
                    edge = structuressjf.get_admitted_edge(admitted_i, e)
                    pending_count = pending_requests[(edge.src, edge.dst)]
                    self.assertTrue(pending_count >= 1)
                    if pending_count > 1:
                        pending_requests[(edge.src, edge.dst)] = pending_count - 1
                    else:
                        del pending_requests[(edge.src, edge.dst)]

                    rack_outputs[structuressjf.get_rack_from_id(edge.src)] += 1
                    rack_inputs[structuressjf.get_rack_from_id(edge.dst)] += 1
            
                for index in range(len(rack_outputs)):
                    self.assertTrue(rack_outputs[index] <= rack_capacity)
                    self.assertTrue(rack_inputs[index] <= rack_capacity)
                
        print 'requested %d, admitted %d, capacity %d' % (num_requested, num_admitted, duration * n_nodes)

        # should clean up memory

        pass

    def test_oversubscribed(self):
        """Tests networks which are oversubscribed on the uplinks from racks/downlinks to racks."""

        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(True, 2, 0, 128, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        # Make requests that could overfill the links above the ToRs
        admissiblesjf.add_backlog(status, 0, 32, 1)
        admissiblesjf.add_backlog(status, 1, 64, 1)
        admissiblesjf.add_backlog(status, 2, 96, 1)
        admissiblesjf.add_backlog(status, 33, 65, 1)
        admissiblesjf.add_backlog(status, 97, 66, 1)
    
        # Get admissible traffic
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
   
        # Check that we admitted at most 2 packets for each of the
        # oversubscribed links
        admitted = admissiblesjf.dequeue_admitted_traffic(status)
        rack_0_out = 0
        rack_2_in = 0
        for e in range(admitted.size):
            edge = structuressjf.get_admitted_edge(admitted, e)
            if structuressjf.get_rack_from_id(edge.src) == 0:
                rack_0_out += 1
            if structuressjf.get_rack_from_id(edge.dst) == 2:
                rack_2_in += 1

        self.assertEqual(rack_0_out, 2)
        self.assertEqual(rack_2_in, 2)

        # should clean up memory

        pass

    def test_out_of_boundary(self):
        """Tests traffic to destinations out of the scheduling boundary."""

        # initialization
        q_bin = fpring.fp_ring_create(structuressjf.NUM_BINS_SHIFT)
        q_urgent = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT + 1)
        q_head = fpring.fp_ring_create(2 * structuressjf.NODES_SHIFT)
        q_admitted_out= fpring.fp_ring_create(structuressjf.BATCH_SHIFT)
        core = structuressjf.create_admission_core_state()
        structuressjf.alloc_core_init(core, q_bin, q_bin, q_urgent, q_urgent)
        status = structuressjf.create_admissible_status(False, 0, 2, 6, q_head,
                                                     q_admitted_out)
        admitted_batch = structuressjf.create_admitted_batch()

        for i in range(0, structuressjf.NUM_BINS):
            empty_bin = structuressjf.create_bin(structuressjf.LARGE_BIN_SIZE)
            fpring.fp_ring_enqueue(q_bin, empty_bin)

        admissiblesjf.enqueue_head_token(q_urgent)

        # Make requests that could overfill the links out of the scheduling boundary
        dst = structuressjf.OUT_OF_BOUNDARY_NODE_ID
        admissiblesjf.add_backlog(status, 0, dst, 1)
        admissiblesjf.add_backlog(status, 1, dst, 1)
        admissiblesjf.add_backlog(status, 2, dst, 1)
        admissiblesjf.add_backlog(status, 3, dst, 1)
        admissiblesjf.add_backlog(status, 4, dst, 1)
        admissiblesjf.add_backlog(status, 5, dst, 1)
    
        # Get admissible traffic
        admissiblesjf.get_admissible_traffic(core, status, admitted_batch, 0, 1, 0)
   
        # Check that we admitted at most 2 out of the boundary per timeslot for
        # first 3 timeslots
        for i in range(0, 3):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 2)
            for e in range(admitted_i.size):
                edge = structuressjf.get_admitted_edge(admitted_i, e)
                self.assertEqual(edge.src, 2 * i + e)
        # Check that we admitted none for the remainder of the batch
        for i in range(3, structuressjf.BATCH_SIZE):
            admitted_i = admissiblesjf.dequeue_admitted_traffic(status)
            self.assertEqual(admitted_i.size, 0)

        # should clean up memory

        pass
      
if __name__ == "__main__":
    unittest.main()
