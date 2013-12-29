#!/usr/bin/python

import numpy

def lines_to_sum_array(lines):
    arrs = map(numpy.array, [map(int, line.split(',')[1:]) for line in lines])
    return reduce(lambda x,y: x + y, arrs)


def process():
    out = file('results/r1-summary.csv','w')
    
    out.write('bin,load,util,pkt_latency,adu_latency,adu1_latency\n')
    
    for load in xrange(600,3401,200):
        pkt_latencies = []
        adu_latencies = []
        adu1_latencies = []
        for node in xrange(6):
            lines = file("results/node_%d_%d.csv" % (node, load)).readlines()
            pkt_latencies.extend(l for l in lines if l.startswith('pkt_latency'))
            adu_latencies.extend(l for l in lines if l.startswith('adu_latency'))
            adu1_latencies.extend(l for l in lines if l.startswith('adu1_latency'))
            clock = int([l for l in lines if l.startswith('clock')][0].split(',')[1])
            bin_size = int([l for l in lines if l.startswith('bin_size')][0].split(',')[1])
            duration = int([l for l in lines if l.startswith('duration,')][0].split(',')[1])
            tx = int([l for l in lines if l.startswith('tx,')][0].split(',')[1])
            bin_time = 1.0 * bin_size / clock
            utilization = tx * 1500 * 8 / (1.0e9 * duration / clock)
        
        pkt_freq = numpy.array(lines_to_sum_array(pkt_latencies), dtype=numpy.float32)
        pkt_freq /= sum(pkt_freq)

        adu_freq = numpy.array(lines_to_sum_array(adu_latencies), dtype=numpy.float32)
        adu_freq /= sum(adu_freq)

        adu1_freq = numpy.array(lines_to_sum_array(adu1_latencies), dtype=numpy.float32)
        adu1_freq /= sum(adu1_freq)
        
        for i in xrange(len(pkt_freq)):
            out.write('%g,%d,%g,%g,%g,%g\n' % (bin_time * (0.5+i), load, utilization,
                                            pkt_freq[i], adu_freq[i], adu1_freq[i]))


if __name__ == '__main__':
    process()