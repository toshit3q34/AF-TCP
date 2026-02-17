#include "io_module.hpp"

void IO_module::load_module(void){
    int ifindex = if_nametoindex(if_name);
    if(!ifindex){
        // Print error here
    }

    // xdp is section name -> refer to scratch/af_xdp_kern.c
    prog = xdp_program__open_file("af_xdp_kern.o","xdp", NULL);
    if(!prog){
        // Error print
    }

    // XDP_FLAGS_DRV_MODE: Native XDP (Fastest, requires driver support)
    // XDP_FLAGS_SKB_MODE: Generic XDP (Slower, works on any driver)
    int err = xdp_program__attach(prog, ifindex, XDP_MODE_NATIVE, 0);
    if(err){
        // Print error
    }
}