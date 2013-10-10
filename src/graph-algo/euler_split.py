'''
Created on Oct 9, 2013

@author: yonch
'''

import networkx as nx

class euler_split(object):
    
    def split(self, g):
        g1 = nx.MultiGraph()
        g2 = nx.MultiGraph()
        
        path_index = {}
        path_node = {}
        path_len = 0
        
        for node in g.nodes_iter():
            cur_node = node
            
            while path_len > 0 or g.degree(node) > 0:
                if cur_node in path_index:
                    # found Euler cycle, partition edges
                    cycle_len = path_len - path_index[cur_node]
                    
                    for i in xrange(0, cycle_len, 2):
                        i #unused
                        # have two edges (u,v), (v,cur_node)
                        u = path_node[path_len - 2]
                        v = path_node[path_len - 1]
                        g1.add_edge(u, v)
                        g2.add_edge(v, cur_node)
                        del path_index[u]
                        del path_index[v]
                        del path_node[path_len - 2]
                        del path_node[path_len - 1]
                        path_len -= 2
                        cur_node = u
                else:
                    path_node[path_len] = cur_node
                    path_index[cur_node] = path_len
                    path_len += 1
                    
                    new_node = g.neighbors(cur_node)[0]
                    g.remove_edge(cur_node, new_node)
                    cur_node = new_node
        return g1,g2
                