'''
Created on Oct 9, 2013

@author: yonch
'''

from euler_split import euler_split

class bin_graph(object):
    def __init__(self, degree, graph):
        self.degree = degree
        self.g = graph

class kapoor_rizzi(object):
    '''
    Implements the Kapoor-Rizzi method to reduce bipartite multigraph edge 
        coloring to multiple Euler-splits and at most one matching search.
    '''
    
    def __init__(self):
        self.euler_split = euler_split()

    def almost_solve(self, d, g):
        print "Normalize:"
        bins = self._normalize(bin_graph(d,g))
        print "After normalize"
        self._print_degrees(bins)
        
        a = bins[0]
        b = bins[1]
        c = bins[2]
        assert (c.degree == b.degree)
        T = bins[3:]

        while (a.degree != b.degree):
            while (b.degree % 2 == 0):
                T = [c] + T
                b, c = self._split_even(b)
                
            self._print_degrees([a,b,c] + T)
            
            if (a.degree != b.degree):
                a,b,c = self._hit_even(a, b, c)
                self._print_degrees([a,b,c] + T)
        
        return [a,b,c] + T
    
    def _normalize(self, d):
        bins = []
        
        if d.degree < 3:
            raise RuntimeError, "need d >= 3"
        
        while d.degree % 2 == 0:
            assert(d.degree >= 3)
            if d.degree == 4:
                print "d/2 == 2"
                g_a, g_b = self._split_even(d)
                g1,g2 = self._split_even(g_a, 2)
                g3,g4 = self._split_even(g_b, 2)
                return [g1,g2,g3,g4] + bins
                
            d,d1 = self._split_even(d)
            bins = [d1] + bins
            
        assert(d.degree >= 3)
        d1, d = self._slice_one(d)
        d2, d3 = self._split_even(d)
        return [d1, d2, d3] + bins
    
    def _hit_even(self, a, b, c):
        '''
        perform HIT-EVEN on (a,b,b)
        '''
        print "Hit-even(%d,%d,%d)" % (a.degree,b.degree,c.degree)
        
        assert(a.degree % 2 == 1)
        assert(b.degree % 2 == 1)
        assert(a.degree != b.degree)
        assert(b.degree == c.degree)
        
        while b.degree % 2 == 1:
            (a, (b, c)) = (c, self._split_odd(a, b)) 
        
        return a, b, c
    
    def _split_even(self, d):
        print "Split-even(%d)" % d.degree 
        #g1,g2 = self.euler_split.split(d.g)
        g1,g2 = None, None
        return bin_graph(d.degree/2, g1), bin_graph(d.degree/2, g2)
    
    def _split_odd(self, d1, d2):
        print "Split-odd(%d,%d)" % (d1.degree,d2.degree)
        d1.g.add_edges_from(d2.g.edges())
        d1.degree += d2.degree
        #g_a, g_b = self.euler_split.split(d1.g)
        g_a, g_b = None, None
        return (d1.degree / 2, g_a), (d1.degree / 2, g_b)
    
    def _slice_one(self, d):
        print "Slice-one(%d)" % d.degree
        return bin_graph(1, None), bin_graph(d.degree - 1, None)
    
    def _print_degrees(self, L):
        print ", ".join(repr(x.degree) for x in L)