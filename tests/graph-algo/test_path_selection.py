'''
Created on December 3, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

from graph_util import graph_util
import pathselection
import structures

class Test(unittest.TestCase):
    
    def test_regular_graph(self):
        """Basic test involving graphs that are already regular."""

        generator = graph_util()
        num_experiments = 10
        n_nodes = 256 # network with 8 racks of 32 nodes each

        for i in range(num_experiments):
            # generate admitted traffic
            g_p = generator.generate_random_regular_bipartite(n_nodes, 1)

            admitted = structures.create_admitted_traffic()
            admitted_copy = structures.create_admitted_traffic()
            for edge in g_p.edges_iter():
                structures.insert_admitted_edge(admitted, edge[0], edge[1] - n_nodes)
                structures.insert_admitted_edge(admitted_copy, edge[0], edge[1] - n_nodes)

            # select paths
            pathselection.select_paths(admitted)

            # check that each edge has a valid path number and that each path
            # is used the correct number of times
            path_counts = []
            num_paths = 4
            for i in range(num_paths):
                path_counts.append(0)

            for e in range(admitted.size):
                edge = structures.get_admitted_edge(admitted, e)
                path = (edge.dst & ~pathselection.PATH_MASK) >> pathselection.PATH_SHIFT
                self.assertTrue(path < num_paths)
                path_counts[path] += 1
        
            for i in range(num_paths):
                self.assertEqual(path_counts[i], n_nodes / 4)

            # check that src addrs and lower bits of destination addrs are unchanged
            for e in range(admitted.size):
                edge = structures.get_admitted_edge(admitted, e)
                edge_copy = structures.get_admitted_edge(admitted_copy, e)
                self.assertEqual(edge.src, edge_copy.src)
                self.assertEqual(edge.dst & pathselection.PATH_MASK,
                                 edge_copy.dst & pathselection.PATH_MASK)

            # clean up
            structures.destroy_admitted_traffic(admitted)

        pass
      
if __name__ == "__main__":
    unittest.main()
