'''
Created on October 27, 2013

@author: aousterh
'''
import sys
import timeit
import unittest

sys.path.insert(0, '../../src/graph-algo')
sys.path.insert(0, '../../bindings/graph-algo')

from graph_util import graph_util
from kr_util import kr_util
from timing_util import *
import graph
import kapoorrizzi

class Test(unittest.TestCase):

    def test(self):
        degree = 40
        n_nodes = 15

        # initialize kr
        generator = kr_util()
        kr = generator.build_kr(n_nodes, degree, print_debug=True)
        
        # generate graph and arbitrary matching
        g_p = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        arbitrary_p = graph_util().generate_random_regular_bipartite(n_nodes, 1)

        # create the C versions of the graphs, and copies
        g_c_structure = graph.create_graph_structure_test(n_nodes)
        g_c = graph.create_graph_edges_test(n_nodes)
        g_c_copy = graph.create_graph_edges_test(n_nodes)
        for edge in g_p.edges_iter():
            graph.add_edge(g_c_structure, g_c, edge[0], edge[1])
        graph.copy_edges(g_c, g_c_copy, n_nodes)
        self.assertEqual(graph.get_max_degree(g_c, n_nodes), degree)
        self.assertEqual(graph.get_max_degree(g_c_copy, n_nodes), degree)

        arbitrary_c = graph.create_graph_edges_test(n_nodes)
        for edge in arbitrary_p.edges_iter():
            graph.add_edge(g_c_structure, g_c, edge[0], edge[1])
            graph.set_edge(g_c_structure, g_c_copy, arbitrary_c, edge[0], edge[1])
        self.assertEqual(graph.get_max_degree(arbitrary_c, n_nodes), 1)

        # solve
        solution = kapoorrizzi.create_matching_set()
        kapoorrizzi.solve(kr, g_c_structure, g_c_copy, arbitrary_c, solution)

        # check solution

        # check that we have the correct number of matchings
        num_matchings = kapoorrizzi.get_num_matchings(solution)
        self.assertEqual(num_matchings, degree + 1)

        # check that each matching is a perfect matching
        for i in range(num_matchings):
            matching = kapoorrizzi.get_matching(solution, i)
            self.assertTrue(graph.is_perfect_matching(matching, n_nodes))

        # check sum of matchings equals the original graph
        matchings_graph_c = graph.create_graph_edges_test(n_nodes)
        for i in range(num_matchings):
            matching = kapoorrizzi.get_matching(solution, i)
            graph.add_edges(matchings_graph_c, matching, n_nodes)
        self.assertTrue(graph.are_equal(matchings_graph_c, g_c, n_nodes))

        # clean up
        kapoorrizzi.destroy_kr(kr)
        kapoorrizzi.destroy_matching_set(solution)
        graph.destroy_graph_structure_test(g_c_structure)
        graph.destroy_graph_edges_test(g_c)
        graph.destroy_graph_edges_test(g_c_copy)
        graph.destroy_graph_edges_test(arbitrary_c)

        pass

    def test_timing(self):

        params = [(20, 15), (40, 15), (20, 30), (40, 30)]
        experiments = 100
        
        for x in params:
            degree = x[0]
            n_nodes = x[1]

            print "\ndegree=%d, n_nodes=%d" % (degree, n_nodes)

            c_times = []
            for i in range(experiments):
                t = timeit.Timer('timer.solve()',
                                 'import timing_util; timer = timing_util.c_timing_util(%d, %d)' % (degree, n_nodes))
                c_times.append(t.timeit(1))

            p_times = []
            for i in range(experiments):
                t = timeit.Timer('timer.solve()',
                                 'import timing_util; timer = timing_util.python_timing_util(%d, %d)' % (degree, n_nodes))
                p_times.append(t.timeit(1))

            c_avg = sum(c_times) / len(c_times)
            print "C implementation:"
            print "min time: (%f)" % min(c_times)
            print "max time: (%f)" % max(c_times)
            print "avg time: (%f)" % c_avg

            p_avg = sum(p_times) / len(p_times)
            print "Python implementation:"
            print "min time: (%f)" % min(p_times)
            print "max time: (%f)" % max(p_times)
            print "avg time: (%f)" % p_avg

if __name__ == "__main__":
    unittest.main()
