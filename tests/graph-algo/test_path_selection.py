'''
Created on January 3, 2014

@author: aousterh
'''
import random
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
        n_racks = n_nodes / structures.MAX_NODES_PER_RACK

        for i in range(num_experiments):
            # generate admitted traffic
            g_p = generator.generate_random_regular_bipartite(n_nodes, 1)

            admitted = structures.create_admitted_traffic()
            admitted_copy = structures.create_admitted_traffic()
            for edge in g_p.edges_iter():
                structures.insert_admitted_edge(admitted, edge[0], edge[1] - n_nodes)
                structures.insert_admitted_edge(admitted_copy, edge[0], edge[1] - n_nodes)

            # select paths
            pathselection.select_paths(admitted, n_racks)

            # check that path assignments are valid
            self.assertTrue(pathselection.paths_are_valid(admitted, n_racks))

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

    def test_irregular_graph(self):
        """Tests graphs that are not necessarily regular - some number of sources
        and destinations have no edges."""

        generator = graph_util()
        num_experiments = 100
        n_nodes = 256 # network with 8 racks of 32 nodes each
        n_racks = n_nodes / structures.MAX_NODES_PER_RACK

        for i in range(num_experiments):
            # generate admitted traffic
            g_p = generator.generate_random_regular_bipartite(n_nodes, 1)

            # choose a number of edges to remove
            num_edges_to_remove = random.randint(1, 256)
            # remove edges
            for j in range(num_edges_to_remove):
                while (True):
                    # choose an edge index at random
                    index = random.randint(0, n_nodes - 1)
                    edge = g_p.edges(index)
                    if edge != []:
                        edge_tuple = edge[0]
                        g_p.remove_edge(edge_tuple[0], edge_tuple[1])
                        break

            admitted = structures.create_admitted_traffic()
            admitted_copy = structures.create_admitted_traffic()
            for edge in g_p.edges_iter():
                structures.insert_admitted_edge(admitted, edge[0], edge[1] - n_nodes)
                structures.insert_admitted_edge(admitted_copy, edge[0], edge[1] - n_nodes)

            # select paths
            pathselection.select_paths(admitted, n_racks)

            # check that path assignments are valid
            self.assertTrue(pathselection.paths_are_valid(admitted, n_racks))

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
