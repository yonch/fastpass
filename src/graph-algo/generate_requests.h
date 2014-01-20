/*
 * generate_requests.h
 *
 *  Created on: January 19, 2014
 *      Author: aousterh
 */

#ifndef GENERATE_REQUESTS_H_
#define GENERATE_REQUESTS_H_

// Stores info needed to generate a stream of requests on demand
struct request_generator {
    double mean_t_btwn_requests;  // mean t for all requests
    double last_request_t;
    uint16_t num_nodes;
};

// Info about a request generated as part of a stream on demand
struct request {
    uint16_t src;
    uint16_t dst;
    double time;
};

// Info about incoming requests generated ahead of time
struct request_info {
    uint16_t src;
    uint16_t dst;
    uint16_t backlog;
    uint16_t timeslot;
};


// Initialize a request_generator, to enable generation of a stream
// of requests. Each sender generates a new request with a mean inter-arrival
// mean_t_btwn_requests
static inline
void init_request_generator(struct request_generator *gen,
                            double mean_t_btwn_requests,
                            double start_time, uint16_t num_nodes) {
    assert(gen != NULL);

    gen->mean_t_btwn_requests = mean_t_btwn_requests / num_nodes;
    gen->last_request_t = start_time;
    gen->num_nodes = num_nodes;
}

// Based on a method suggested by wikipedia
// http://en.wikipedia.org/wiki/Exponential_distribution
static inline
double generate_exponential_variate(double mean_t_btwn_requests)
{
  assert(mean_t_btwn_requests > 0);

  double u = rand() / ((double) RAND_MAX);
  return -log(u) * mean_t_btwn_requests;
}

// Populate a request with info about the next request
static inline
void get_next_request(struct request_generator *gen, struct request *req) {
    assert(gen != NULL);
    assert(req != NULL);

    double inter_arrival_t = generate_exponential_variate(gen->mean_t_btwn_requests);
    req->time = gen->last_request_t + inter_arrival_t;
    gen->last_request_t = req->time;

    req->src = rand() / ((double) RAND_MAX) * gen->num_nodes;
    req->dst = rand() / ((double) RAND_MAX) * (gen->num_nodes - 1);
    if (req->dst >= req->src)
        req->dst++;  // Don't send to self
}

// Generate a sequence of requests with Poisson arrival times, puts them in edges
// Returns the number of requests generated
static inline
uint32_t generate_requests_poisson(struct request_info *edges, uint32_t size,
                                   uint32_t num_nodes, uint32_t duration,
                                   double fraction, double mean)
{
    assert(edges != NULL);

    // Generate a sequence of requests with Poisson arrival times per sender
    // and receivers chosen uniformly at random

    // Uses the on-demand request generator to generate requests
    // Convert mean from micros to nanos
    struct request_generator gen;
    struct request req;
    init_request_generator(&gen, mean / fraction, 0, num_nodes);

    struct request_info *current_edge = edges;
    uint32_t num_generated = 0;
    double current_time = 0;
    double fractional_demand = 0;
    while (current_time < duration) {
        get_next_request(&gen, &req);
        double new_demand = generate_exponential_variate(mean);
        current_time = req.time;
        if (new_demand + fractional_demand < 1) {
            fractional_demand += new_demand;
            continue;
        }
        current_edge->src = req.src;
        current_edge->dst = req.dst;
        new_demand += fractional_demand;
        current_edge->backlog = (uint16_t)new_demand;
        fractional_demand = new_demand - (uint16_t)new_demand;
        if (current_edge->backlog == 0) {
            printf("oops\n");
        }
        current_edge->timeslot = (uint16_t) current_time;
        num_generated++;
        current_edge++;
    }

    assert(num_generated <= size);

    return num_generated;
}

#endif /* GENERATE_REQUESTS_H_ */
