
———————— Documantation for Application Layer Protocol ————————
/*                  !!! This system assumes: !!!                 */ 
/* 1. minimum spec of device using this system is 32-bit systems */
/* 2. Runs ONLY under IPv6                                       */
/* ------------------------------------------------------------- */


The RPC system transfers bytestream of the data that are consist of 3 parts:
           < Serialized Data Diagram >
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+ 
|  Command Flag:                        (8byte) | 
|             0: find 1: call                   | 
|                                               | 
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|  Function File Descriptor:            (8byte) | 
|            -1: No Function exist              | 
|          else: file_descriptor(uint64_t)      | 
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|  RPC_DATA:                       (100016byte) |  The size of the RPC_DATA (INSIDE serialized data) is fixed as well like:
|                                               |  data1: 8 byte
|                                               |  data2_len: 8 byte
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+  data2: 100000 byte

Each time the data is sent or received the system will always expact the fixed length of the byte, which is 100032bytes.

Note* that data1 and data2_len will be converted to any size that the host system originally have (e.g. host1 <---> network <---> host2).
                                                                                                       int32       uint64_t      int16


Protocol description:

1. rpc_find() <---> rpc_serve_all():
     client             server

    find() always sends:
        command flag: 0
        func F.D: -1
        rpc_data: 
                    data1: 0
                    data2_len: <length of the function name>
                    data2: <function name>

    if function name exists in server data:
    serve_all() will send:
        command flag: 0
        func F.D: <func_fd(int) of the function with that name>
        rpc_data: 
                    data1: 0
                    data2_len: 0
                    data2: NULL

    if function name does NOT exists in server data:
    serve_all() will send:
        command flag: 0
        func F.D: -1
        rpc_data: 
                    data1: 0
                    data2_len: 0
                    data2: NULL

2. rpc_call() <---> rpc_serve_all():
     client             server

    call() always sends:
        command flag: 1
        func F.D: <func_fd(int) of the function>
        rpc_data: <given from user>

    if func_fd exists in server data:
    serve_all() will send:
        command flag: 1
        func F.D: <func_fd(int) of the function>
        rpc_data: <returned value using the function>

    if function name does NOT exists in server data OR result value is INVALID format:
    serve_all() will send:
        command flag: 1
        func F.D: -1
        rpc_data: 
                    data1: 0
                    data2_len: 0
                    data2: NULL


