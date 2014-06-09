/*
 * generate_requests.h
 *
 *  Created on: January 19, 2014
 *      Author: aousterh
 */

#ifndef GENERATE_REQUESTS_H_
#define GENERATE_REQUESTS_H_

#include <assert.h>
#include <math.h>
#include <stdlib.h>

/**
 * Used Numerical Recipes
 * http://en.wikipedia.org/wiki/Linear_congruential_generator
 */
#define REQ_GEN_RAND_A		1664525
#define REQ_GEN_RAND_C		1013904223

#define REQ_GEN_LOG_TABLE_SIZE		(1 << 12)

// Stores info needed to generate a stream of requests on demand
struct request_generator {
    double mean_t_btwn_requests;  // mean t for all requests
    double last_request_t;
    uint16_t num_nodes;
    uint32_t rand_state;
    double fractional_demand;
    double mean_request_size;
    double exp_dist_table[REQ_GEN_LOG_TABLE_SIZE];
};

// Info about a request generated as part of a stream on demand
struct request {
    uint16_t src;
    uint16_t dst;
    uint16_t backlog;
    double time;
};

// Info about incoming requests generated ahead of time
struct request_info {
    uint16_t src;
    uint16_t dst;
    uint16_t backlog;
    uint16_t timeslot;
};

static inline
void reinit_request_generator(struct request_generator* gen,
		double mean_t_btwn_requests, double start_time, uint16_t num_nodes,
		double mean_request_size)
{
	assert(gen != NULL);

	gen->mean_t_btwn_requests = mean_t_btwn_requests / num_nodes;
	gen->last_request_t = start_time;
	gen->num_nodes = num_nodes;
	gen->fractional_demand = 0.0;
	gen->mean_request_size = mean_request_size;
}

// Initialize a request_generator, to enable generation of a stream
// of requests. Each sender generates a new request with a mean inter-arrival
// mean_t_btwn_requests
static inline
void init_request_generator(struct request_generator *gen,
                            double mean_t_btwn_requests,
                            double start_time, uint16_t num_nodes,
                            double mean_request_size) {
    int i;

	reinit_request_generator(gen, mean_t_btwn_requests, start_time, num_nodes,
			mean_request_size);

	gen->rand_state = rand();

	// Based on a method suggested by wikipedia
	// http://en.wikipedia.org/wiki/Exponential_distribution
	for (i = 0; i < REQ_GEN_LOG_TABLE_SIZE; i++) {
    	  double u = rand() / ((double) RAND_MAX);
    	  gen->exp_dist_table[i] =  -log(u);
    }
}

static inline
double generate_exponential_variate(struct request_generator *gen,
		double mean_t_btwn_requests)
{
	uint32_t table_index;
  assert(mean_t_btwn_requests > 0);

  table_index = ((gen->rand_state >> 16) * REQ_GEN_LOG_TABLE_SIZE) >> 16;
  gen->rand_state = gen->rand_state * REQ_GEN_RAND_A + REQ_GEN_RAND_C;

  return gen->exp_dist_table[table_index] * mean_t_btwn_requests;
}

// Populate a request with info about the next request
static inline
void get_next_request(struct request_generator *gen, struct request *req) {
    assert(gen != NULL);
    assert(req != NULL);

    double inter_arrival_t = 0.0;
    double new_demand = gen->fractional_demand;

    do {
    	inter_arrival_t += generate_exponential_variate(gen, gen->mean_t_btwn_requests);
    	new_demand += generate_exponential_variate(gen, gen->mean_request_size);
    } while ((uint16_t)new_demand < 1);

    req->time = gen->last_request_t + inter_arrival_t;
    gen->last_request_t = req->time;
    req->backlog = (uint16_t)new_demand;
    gen->fractional_demand = new_demand - (uint16_t)new_demand;

    req->src = ((gen->rand_state >> 16) * gen->num_nodes) >> 16;
    gen->rand_state = gen->rand_state * REQ_GEN_RAND_A + REQ_GEN_RAND_C;
    req->dst = ((gen->rand_state >> 16) * (gen->num_nodes - 1)) >> 16;
    gen->rand_state = gen->rand_state * REQ_GEN_RAND_A + REQ_GEN_RAND_C;
    if (req->dst >= req->src)
        req->dst++;  // Don't send to self
}

// Helper method for benchmarking schedule quality in Python
static inline
struct request *create_next_request(struct request_generator *gen) {
    assert(gen != NULL);
    
    struct request *req = malloc(sizeof(struct request));
    if (req == NULL)
        return NULL;

    get_next_request(gen, req);

    return req;
}

// Helper method for testing in Python
static inline
void destroy_request(struct request *req) {
    assert(req != NULL);

    free(req);
}

// Helper method for testing in Python
static inline
struct request_generator *create_request_generator(double mean_t_btwn_requests,
                                                   double start_time,
                                                   uint16_t num_nodes,
                                                   double mean_request_size) {
    struct request_generator *gen = malloc(sizeof(struct request_generator));
    if (gen == NULL)
        return NULL;

    init_request_generator(gen, mean_t_btwn_requests, start_time, num_nodes,
    		mean_request_size);

    return gen;
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
    init_request_generator(&gen, mean / fraction, 0, num_nodes, mean);

    struct request_info *current_edge = edges;
    uint32_t num_generated = 0;
    double current_time = 0.0;
    while (current_time < duration) {
        get_next_request(&gen, &req);
        current_edge->src = req.src;
        current_edge->dst = req.dst;
        current_edge->backlog = req.backlog;
        current_edge->timeslot = (uint16_t) req.time;
        if (current_edge->backlog == 0) {
            printf("oops\n");
        }
        num_generated++;
        current_edge++;
        current_time = req.time;
    }

    assert(num_generated <= size);

    return num_generated;
}

#endif /* GENERATE_REQUESTS_H_ */
