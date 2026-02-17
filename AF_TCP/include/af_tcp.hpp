#include "io_module.hpp"

class AF_TCP_manager {
public:
    AF_TCP_manager(int cpu_id);
    ~AF_TCP_manager();

    // The core execution loop
    void RunMainLoop();

private:
    /* 1. Identity */
    int cpu_id;

    /* 2. Memory Management (The pools we discussed) */
    // Using raw pointers to simulate the mTCP mem_pool logic
    struct mem_pool* flow_pool;       // For tcp_stream
    struct mem_pool* rv_pool;         // For receive variables
    struct mem_pool* sv_pool;         // For send variables

    /* 3. Flow Tracking */
    struct hashtable* tcp_flow_table; // To find streams by 4-tuple
    uint32_t flow_cnt;                // Number of active connections

    /* 4. Event Queues (Raw Linked Lists/Queues) */
    // These track which streams need action (sending data, acks, etc.)
    struct stream_queue* send_queue;
    struct stream_queue* ack_queue;
    struct stream_queue* close_queue;

    /* 5. I/O Integration */
    // This will point to your AF_XDP logic
    IO_module* iom; 
    // void* io_private_context;         // To store your XSK FDs and Rings -> Add this inside iom

    /* 6. Timing and Retransmission */
    uint32_t cur_ts;                  // Current timestamp for this loop
    struct rto_hashstore* rto_store;  // Retransmission timeouts

    /* 7. Application Interface */
    struct mtcp_epoll* ep;            // mTCP-style epoll context

    /* Internal Processing Methods */
    void ProcessPackets();
    void HandleApplicationCalls();
    void WritePackets();
};

class AF_TCP_thread_context{

};