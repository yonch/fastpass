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
            # is used the correct number of times per src/dst rack
            num_paths = pathselection.NUM_PATHS
            src_rack_path_counts = []
            dst_rack_path_counts = []
            for i in range(pathselection.NUM_RACKS):
                src_rack_path_counts.append([])
                dst_rack_path_counts.append([])
                for j in range(num_paths):
                    src_rack_path_counts[i].append(0)
                    dst_rack_path_counts[i].append(0)

            for e in range(admitted.size):
                edge = structures.get_admitted_edge(admitted, e)
                path = (edge.dst & ~pathselection.PATH_MASK) >> pathselection.PATH_SHIFT
                self.assertTrue(path < num_paths)
                src = structures.get_rack_from_id(edge.src)
                dst = structures.get_rack_from_id(edge.dst & pathselection.PATH_MASK)
                src_rack_path_counts[src][path] += 1
                dst_rack_path_counts[dst][path] += 1

            # check that per-rack path counts are valid
            rack_degree = n_nodes / pathselection.NUM_RACKS
            for i in range(pathselection.NUM_RACKS):
                for j in range(num_paths):
                    self.assertTrue(src_rack_path_counts[i][j] <= rack_degree / num_paths)
                    self.assertTrue(dst_rack_path_counts[i][j] <= rack_degree / num_paths)

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
            pathselection.select_paths(admitted)

            # calculate the max degree into or out of a rack
            num_paths = pathselection.NUM_PATHS
            src_rack_degrees = []
            dst_rack_degrees = []
            for i in range(pathselection.NUM_RACKS):
                src_rack_degrees.append(0)
                dst_rack_degrees.append(0)
        
            for e in range(admitted_copy.size):
                edge = structures.get_admitted_edge(admitted_copy, e)
                src_rack = structures.get_rack_from_id(edge.src)
                dst_rack = structures.get_rack_from_id(edge.dst)
                src_rack_degrees[src_rack] += 1
                dst_rack_degrees[dst_rack] += 1
            
            max_degree = max(src_rack_degrees + dst_rack_degrees)
            if max_degree % num_paths != 0:
                max_degree = (max_degree / num_paths + 1) * num_paths

            # check that each edge has a valid path number and that each path
            # is used the correct number of times per src/dst rack
            src_rack_path_counts = []
            dst_rack_path_counts = []
            for i in range(pathselection.NUM_RACKS):
                src_rack_path_counts.append([])
                dst_rack_path_counts.append([])
                for j in range(num_paths):
                    src_rack_path_counts[i].append(0)
                    dst_rack_path_counts[i].append(0)

            for e in range(admitted.size):
                edge = structures.get_admitted_edge(admitted, e)
                path = (edge.dst & ~pathselection.PATH_MASK) >> pathselection.PATH_SHIFT
                self.assertTrue(path < num_paths)
                src = structures.get_rack_from_id(edge.src)
                dst = structures.get_rack_from_id(edge.dst & pathselection.PATH_MASK)
                src_rack_path_counts[src][path] += 1
                dst_rack_path_counts[dst][path] += 1

            # check that per-rack path counts are valid
            for i in range(pathselection.NUM_RACKS):
                for j in range(num_paths):
                    self.assertTrue(src_rack_path_counts[i][j] <= max_degree / num_paths)
                    self.assertTrue(dst_rack_path_counts[i][j] <= max_degree / num_paths)

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
