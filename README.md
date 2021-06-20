# Multiplayer Server

---
CPS2008 *Operating Systems and Systems Programming 2*

Assignment for the Academic year 2020/1

Xandru Mifsud (0173498M), B.Sc. (Hons) Mathematics and Computer Science

---

This repository defines the server implementation intended to communicate with a client running a front-end that makes use
of the corresponding shared library. For an example front-end implementation, kindly see our [multiplayer tetris](https://github.com/xmif1/CPS2008_Tetris_FrontEnd)
variant.

The server is in particular responsible for maintaining leaderboards, player statistics, distributing live-chat messages
received to all the active client, starting new game sessions, etc...

The implementation carries includes a number of cleanup functions and signal handlers, which ensure graceful disconnection
and closure with clients. Significant effort was put into ensuring clean disconnection, including ensuring that multi-threaded
transactions are atomic (a principle borrowed from databases), etc. This is detailed in the introductory chapter of the
assignment report.

## Requirements

This program is intended to run on Linux platforms, in particular on Debian-based systems such as Ubuntu and
Lubuntu, on which we have tested the implementation with relative success.

For installation, ```cmake``` version 3.17+ is required.

## Installation Instructions

Clone the repository, and ```cd``` into the project directory. Then run:

1. ```cmake .```
2. ```make```

## Execution Instructions

Simply ```cd``` into the directory containing the compiled executable, and run ```./CPS2008_Tetris_Server```.

## Known Issues

The server in particular carries out a handshake of sorts with clients during P2P setup, where it does not signal for a 
game to start until all clients in the game session have sent a ```P2P_READY``` message. There are a number of known 
issues during P2P setup which can occur, albeit they seem to be sporadic. There are also issues with socket binding when
restarting the server shortly after termination.

These issues are outlined in detail in the *Testing* and the *Limitations and Future Improvements* chapters of the
assignment report. In summary:

1. A client may send a ```P2P_READY``` message to the server, but it may never receive a ```START_GAME``` message, resulting
   in an indefinite wait. We suspect this issue to lie within the server implementation and how we keep track of the number
   of ```P2P_READY``` messages received across multiple threads, using ```pthread_cond_wait``` and ```pthread_cond_broadcast```.
   Indeed, we believe that some mutex lock (that of the ```game_session``` struct) is not being released as expected,
   causing a deadlock in the server such that it never sends a ```START_GAME``` message.
2. Restarting the server shortly after termination may result in a socket binding error. To mitigate this, we tried using
   ```setsockopt(SO_REUSEADDR)```, however the issue persisted.
   
## Limitations

1. The current implementation does not support any form of data persistence and hence player information is lost on
   disconnection of either that client, or of the server. One future improvement which would be nice to have is thread-safe
   data persistence and a login system for clients.
