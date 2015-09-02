#ifndef METH1_CLIENT2_HH
#define METH1_CLIENT2_HH

#include "address.hh"
#include "buffered_io.hh"
#include "socket.hh"
#include "timestamp.hh"

#include "record.hh"

/**
 * Strategy 1.
 * - No upfront work.
 * - Full linear scan for each record.
 */
namespace meth1
{

/**
 * Client communicates with a single backend node. Performs some basic caching
 * of records read from the network, mostly to allow asynchronous IO.
 */
class Client
{
public:
  /* network state */
  BufferedIO_O<TCPSocket> sock_;
  Address addr_;

  /* rpc state */
  clk::time_point rpcStart_;
  uint64_t rpcPos_;

  /* debug info */
  size_t sendPass_;
  size_t recvPass_;
  size_t sizePass_;

public:
  Client( Address node );

  /* Perform a read. Return value is number of records available to read */
  void sendRead( uint64_t pos, uint64_t size );
  uint64_t recvRead( void );
  RecordPtr readRecord( void );

  /* Return the number of records available at this server */
  void sendSize( void );
  uint64_t recvSize( void );

  /* Shutdown the backend node */
  void sendShutdown( void );
};
}

#endif /* METH1_CLIENT2_HH */
