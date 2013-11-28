'''
Created on November 27, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import structures
import admissible

class Test(unittest.TestCase):
    
    def test_one_request(self):
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

        # TODO: write more tests
                        

if __name__ == "__main__":
    unittest.main()
