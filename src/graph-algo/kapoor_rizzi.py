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

    def almost_solve(self, d, g, arbitrary_matchings = []):
        '''
        Performs ALMOST-SOLVE(delta).
        @param d: degree of each node (g is regular)
        @param g: regular graph with degrees d
        @param arbitrary_matchings: a list of arbitrary permutations to use instead
            of SLICE-ONE. If d is even, one is needed; if odd, two.
        '''
        print "Normalize:"
        if (len(arbitrary_matchings) == 0):
            bins = self._normalize(bin_graph(d,g))
        else:
            # use the arbitrary matchings
            bins = self._approx_normalize(
                bin_graph(d,g), [bin_graph(1,x) for x in arbitrary_matchings])
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
    
    def solve(self, d, g, arbitrary_matchings = []):
        bins = self.almost_solve(d, g, arbitrary_matchings)
        
        matchings = []
        work_list = bins
        work_list.reverse() # want to use pop and append, so need to reverse

        while (len(work_list) > 0):
            g = work_list.pop()
            while (g.degree != 1):
                self._print_degrees(matchings + [g] + list(reversed(work_list)))
                if (g.degree % 2 == 1):
                    # add a matching, and split
                    g, g1 = self._split_odd(g, matchings.pop())
                else:
                    # just split
                    g, g1 = self._split_even(g)
                # save g1 for later
                work_list.append(g1)
            # okay, got a matching
            matchings.append(g)
        
        self._print_degrees(matchings + [g] + list(reversed(work_list)))
        
        return [x.g for x in matchings]
            
    
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
    
    def _approx_normalize(self, d, arbitrary_matchings):
        # sanity check
        if (len(arbitrary_matchings) != ((d.degree % 2) + 1)):
            raise RuntimeError, "wrong number of arbitrary matchings for d=%d, (got %d, need exactly %d)" % \
                (d, len(arbitrary_matchings), ((d.degree % 2) + 1))
        
        # use the arbitrary matchings
        if (d.degree % 2 == 1):
            g1, g2 = self._split_odd(d, arbitrary_matchings[1]) 
        else:
            g1, g2 = self._split_even(d)
        
        return [arbitrary_matchings[0], g1, g2]

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
        g1,g2 = self.euler_split.split(d.g)
        return bin_graph(d.degree/2, g1), bin_graph(d.degree/2, g2)
    
    def _split_odd(self, d1, d2):
        print "Split-odd(%d,%d)" % (d1.degree,d2.degree)
        d1.g.add_edges_from(d2.g.edges())
        d1.degree += d2.degree
        g_a, g_b = self.euler_split.split(d1.g)
        return bin_graph(d1.degree / 2, g_a), bin_graph(d1.degree / 2, g_b)
    
    def _slice_one(self, d):
        print "Slice-one(%d)" % d.degree
        raise RuntimeError, "Slice-one not currently implemented, try the approximation version"
        #return bin_graph(1, None), bin_graph(d.degree - 1, None)
    
    def _print_degrees(self, L):
        print ", ".join(repr(x.degree) for x in L)