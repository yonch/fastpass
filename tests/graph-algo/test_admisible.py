'''
Created on November 27, 2013

@author: aousterh
'''
import random
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import structures
import admissible

class Test(unittest.TestCase):
    
    def test_one_request(self):
        """Basic test involving one src/dst pair."""

        queue_0 = structures.create_backlog_queue()
        queue_1 = structures.create_backlog_queue()
        new_requests = structures.create_backlog_queue()
        admitted = structures.create_admitted_traffic()
        status = structures.create_flow_status()

        # Make a request, check it was inserted correctly
        admissible.request_timeslots(new_requests, status, 0, 1, 5)
        self.assertEqual(new_requests.head, 0)
        self.assertEqual(new_requests.tail, 1)
        self.assertEqual(new_requests.edges.src, 0)
        self.assertEqual(new_requests.edges.dst, 1)
        self.assertEqual(new_requests.edges.backlog, 5)

        # Get admissible traffic (timeslot 1)
        admissible.get_admissible_traffic(queue_0, queue_1, new_requests,
                                          admitted, status)

        # Check that one packet was admitted, queue_0 is still empty, and
        # queue_1 has the traffic with one fewer backlog item
        self.assertEqual(admitted.size, 1)
        self.assertEqual(admitted.edges.src, 0)
        self.assertEqual(admitted.edges.dst, 1)
        self.assertEqual(admitted.edges.remaining_backlog, 4)
        self.assertEqual(queue_0.head, 0)
        self.assertEqual(queue_0.tail, 0)
        self.assertEqual(queue_1.head, 0)
        self.assertEqual(queue_1.tail, 1)
        self.assertEqual(queue_1.edges.src, 0)
        self.assertEqual(queue_1.edges.dst, 1)
        self.assertEqual(queue_1.edges.backlog, 4)
        self.assertEqual(queue_1.edges.timeslot, 1)
   
        structures.destroy_backlog_queue(queue_0)
        structures.destroy_backlog_queue(queue_1)
        structures.destroy_backlog_queue(new_requests)
        structures.destroy_admitted_traffic(admitted)
        structures.destroy_flow_status(status)

        pass

    def test_multiple_competing_requests(self):
        """Test of two flows from the same source. """

        queue_0 = structures.create_backlog_queue()
        queue_1 = structures.create_backlog_queue()
        new_requests = structures.create_backlog_queue()
        admitted_0 = structures.create_admitted_traffic()
        admitted_1 = structures.create_admitted_traffic()
        admitted_2 = structures.create_admitted_traffic()
        admitted_3 = structures.create_admitted_traffic()
        status = structures.create_flow_status()
        empty_queue = structures.create_backlog_queue()

        # Make a few competing requests
        admissible.request_timeslots(new_requests, status, 0, 1, 2)
        admissible.request_timeslots(new_requests, status, 0, 4, 1)

        # Get admissible traffic (timeslot 1), check that one packet was admitted
        admissible.get_admissible_traffic(queue_0, queue_1, new_requests,
                                          admitted_0, status)
        self.assertEqual(admitted_0.size, 1)

        # Get admissible traffic (timeslot 2), check that one packet was admitted
        admissible.get_admissible_traffic(queue_1, queue_0, empty_queue,
                                          admitted_1, status)
        self.assertEqual(admitted_1.size, 1)

        # Get admissible traffic (timeslot 3), check that one packet was admitted
        admissible.get_admissible_traffic(queue_0, queue_1, empty_queue,
                                          admitted_2, status)
        self.assertEqual(admitted_2.size, 1)

        # Get admissible traffic (timeslot 4), check that no packets were admitted
        admissible.get_admissible_traffic(queue_1, queue_0, empty_queue,
                                          admitted_3, status)
        self.assertEqual(admitted_3.size, 0)

        structures.destroy_backlog_queue(queue_0)
        structures.destroy_backlog_queue(queue_1)
        structures.destroy_backlog_queue(new_requests)
        structures.destroy_admitted_traffic(admitted_0)
        structures.destroy_admitted_traffic(admitted_1)
        structures.destroy_admitted_traffic(admitted_2)
        structures.destroy_admitted_traffic(admitted_3)
        structures.destroy_flow_status(status)
        structures.destroy_backlog_queue(empty_queue)

        pass

    def test_out_of_order_requests(self):
        """Tests that requests are admitted in the order of last send timeslot,
        regardless of the order the requests arrive in."""

        queue_0 = structures.create_backlog_queue()
        queue_1 = structures.create_backlog_queue()
        new_requests = structures.create_backlog_queue()
        admitted_0 = structures.create_admitted_traffic()
        admitted_1 = structures.create_admitted_traffic()
        admitted_2 = structures.create_admitted_traffic()
        admitted_3 = structures.create_admitted_traffic()
        status = structures.create_flow_status()
        empty_queue = structures.create_backlog_queue()

        # Make two competing requests
        admissible.request_timeslots(new_requests, status, 3, 5, 1)
        admissible.request_timeslots(new_requests, status, 4, 5, 1)
    
        # Get admissible traffic (timeslot 1)
        # Check that one packet was admitted from src 3
        admissible.get_admissible_traffic(queue_0, queue_1, new_requests,
                                          admitted_0, status)
        self.assertEqual(admitted_0.size, 1)
        self.assertEqual(admitted_0.edges.src, 3)

        # Get admissible traffic (timeslot 2)
        # Check that one packet was admitted from src 4
        admissible.get_admissible_traffic(queue_1, queue_0, empty_queue,
                                          admitted_1, status)
        self.assertEqual(admitted_1.size, 1)
        self.assertEqual(admitted_1.edges.src, 4)

        # Make two competing requests out of their timeslot order
        structures.init_backlog_queue(new_requests)
        admissible.request_timeslots(new_requests, status, 4, 5, 1)
        admissible.request_timeslots(new_requests, status, 3, 5, 1)

        # Get admissible traffic (timeslot 3)
        # Check that one packet was admitted from src 3
        admissible.get_admissible_traffic(queue_0, queue_1, new_requests,
                                          admitted_2, status)
        self.assertEqual(admitted_2.size, 1)
        self.assertEqual(admitted_2.edges.src, 3)

        # Get admissible traffic (timeslot 4)
        # Check that one packet was admitted from src 4
        admissible.get_admissible_traffic(queue_1, queue_0, empty_queue,
                                          admitted_3, status)
        self.assertEqual(admitted_3.size, 1)
        self.assertEqual(admitted_3.edges.src, 4)

        structures.destroy_backlog_queue(queue_0)
        structures.destroy_backlog_queue(queue_1)
        structures.destroy_backlog_queue(new_requests)
        structures.destroy_admitted_traffic(admitted_0)
        structures.destroy_admitted_traffic(admitted_1)
        structures.destroy_admitted_traffic(admitted_2)
        structures.destroy_admitted_traffic(admitted_3)
        structures.destroy_flow_status(status)
        structures.destroy_backlog_queue(empty_queue)

        pass

    def test_backlog_sorting_1_item(self):
        """Tests sorting of backlogs with 1 item."""

        queue = structures.create_backlog_queue()

        # Enqueue item
        structures.enqueue_backlog(queue, 0, 1, 2, 3)

        # Sort
        structures.sort_backlog(queue)
       
        # Check order
        edge = structures.peek_head_backlog(queue)
        self.assertEqual(edge.src, 0)
        self.assertEqual(edge.dst, 1)
        self.assertEqual(edge.backlog, 2)
        self.assertEqual(edge.timeslot, 3)
        structures.dequeue_backlog(queue)
        self.assertEqual(structures.is_empty_backlog(queue), True)

        structures.destroy_backlog_queue(queue)

        pass

    def test_backlog_sorting_many_items(self):
        """Tests sorting of backlogs with many items."""

        for num_items in xrange(2, 50, 3):
            queue = structures.create_backlog_queue()

            # Enqueue items
            timeslots = []
            for i in xrange(num_items):
                t = random.randint(0, num_items)
                structures.enqueue_backlog(queue, 0, 1, 1, t)
                timeslots.append(t)

            # Sort
            structures.sort_backlog(queue)

            # Check order and validity of timeslots
            edge_1 = structures.peek_head_backlog(queue)
            structures.dequeue_backlog(queue)
            self.assertIn(edge_1.timeslot, timeslots)
            timeslots.remove(edge_1.timeslot)
            while structures.is_empty_backlog(queue) == False:
                edge_0 = edge_1
                edge_1 = structures.peek_head_backlog(queue)
                structures.dequeue_backlog(queue)
                self.assertTrue(edge_0.timeslot <= edge_1.timeslot)
                
                self.assertIn(edge_1.timeslot, timeslots)
                timeslots.remove(edge_1.timeslot)

            self.assertEqual(structures.is_empty_backlog(queue), True)
            self.assertEqual(len(timeslots), 0)

        pass

 

    # TODO: write more tests
                        

if __name__ == "__main__":
    unittest.main()
