'''
Created on October 28, 2013

@author: aousterh
'''
import math
import random
import sys
import unittest

sys.path.insert(0, '../../src/graph-algo')
sys.path.insert(0, '../../bindings/graph-algo')

import admissible
from graph_util import graph_util
from kapoor_rizzi import *
from kr_util import kr_util
import graph
import kapoorrizzi
import structures

class c_timing_util(object):
    '''
    Functions for timing kr code C implementation
    '''

    def __init__(self, degree, n_nodes):
        # initialize kr
        generator = kr_util()
        self.kr = generator.build_kr(n_nodes, degree)

        g_p = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        arbitrary_p = graph_util().generate_random_regular_bipartite(n_nodes, 1)
        
        # create the C versions of the graphs
        self.g_c_structure = graph.create_graph_structure_test(n_nodes)
        self.g_c = graph.create_graph_edges_test(n_nodes)
        self.g_c_copy = graph.create_graph_edges_test(n_nodes)
        for edge in g_p.edges_iter():
            graph.add_edge(self.g_c_structure, self.g_c, edge[0], edge[1])
        graph.copy_edges(self.g_c, self.g_c_copy, n_nodes)
 
        self.arbitrary_c = graph.create_graph_edges_test(n_nodes)
        for edge in arbitrary_p.edges_iter():
            graph.add_edge(self.g_c_structure, self.g_c, edge[0], edge[1])
            graph.set_edge(self.g_c_structure, self.g_c_copy, self.arbitrary_c, edge[0], edge[1])

        # allocate solution
        self.solution = kapoorrizzi.create_matching_set()

    def solve(self):
        kapoorrizzi.solve(self.kr, self.g_c_structure, self.g_c_copy, self.arbitrary_c, self.solution)

class python_timing_util(object):
    '''
    Functions for timing kr code Python implementation
    '''

    def __init__(self, degree, n_nodes):
        self.degree = degree
        self.KR = kapoor_rizzi()
    
        # generate random graph
        self.g = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        
        # generate arbitrary partitions for approximation algo
        self.arbitrary = [graph_util().generate_random_regular_bipartite(n_nodes, 1) for i in xrange((degree % 2) + 1)]
    

    def solve(self):
        solution = self.KR.solve(self.degree, self.g, self.arbitrary)

class admissible_timing_util(object):
    '''
    Functions for timing admissible traffic code
    '''

    def __init__(self, n, duration):
        # generate a series of requests, each specified by a tuple per timeslot
        # (src, dst, # packets)
        # each src requests a fixed number of packets to a destination chosen
        # uniformly at random with intervals between send times specified by
        # an exponential distribution
        self.n = n
        self.duration = duration
        self.requests = []
        for t in range(self.duration):
          self.requests.append([]) # an empty list of requests per timeslot

        # note: over long time scales, we should just barely be able to admit
        # all traffic
        mean = 20
        fraction = 0.95
        packets_requested = int(mean * fraction)
        for src in range(n):
            t = self.gen_exponential_variate(mean)
            cumulative_demand = 0
            while (t < self.duration):
                cumulative_demand += packets_requested
                dst = random.randint(0, n-2)
                if (dst >= src):
                    dst += 1  # don't send to self
                self.requests[int(t)].append((src, dst, cumulative_demand))
                t = t + self.gen_exponential_variate(mean)

        # initialize structures
        self.queue_0 = structures.create_backlog_queue()
        self.queue_1 = structures.create_backlog_queue()
        self.new_requests = structures.create_backlog_queue()
        self.admitted = structures.create_admitted_traffic()
        self.status = structures.create_admissible_status()

    def gen_exponential_variate(self, mean):
        '''
        Generates an exponential variate from a distribution with the specified mean
        '''
        return -math.log(random.uniform(0, 1)) * mean

    def run(self):
        admitted = 0.0

        for t in range(self.duration):
            structures.init_backlog_queue(self.new_requests)
            # Make new requests
            for r in self.requests[t]:
                admissible.request_timeslots(self.new_requests, self.status,
                                             r[0], r[1], r[2])

            # Get admissible traffic for this timeslot
            if t % 2 == 0:
                queue_in = self.queue_0
                queue_out = self.queue_1
            else:
                queue_in = self.queue_1
                queue_out = self.queue_0

            structures.init_admitted_traffic(self.admitted)
            structures.init_backlog_queue(queue_out)
            admissible.get_admissible_traffic(queue_in, queue_out, self.new_requests,
                                              self.admitted, self.status)
            admitted += self.admitted.size

        print 'network utilization: %f' % (admitted / (self.duration * self.n))
 
