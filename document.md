# ChatBot for an IRC Server

## Features 
- Uses Ollama to generate responses.
- Uses UNIX internals for smooth operation
- Monitors all defined channels at once.
- Logs channel messages and generated responses.
- Admin channel and admin commands.
- Ability to mute users (only allowed for admin user).
- Ability to shutdown the program, without leaving the IRC server (only allowed for admin user).

## UNIX Internals

### Sockets
- Used to connect to a specified server.

### Forking
- Chat Bot runs several processes at once.
- One process is used for monitoring all the socket input sent from the server.
- Several processes (up to 32, depends on how many are defined in the config file) are used for interacting with IRC channels.
- One process is used for generating responses using a LLM.
- One process is used to monitor an admin channel and identify specified commands, and execute on them. 

### Semaphores
- Used for monitoring when log file is ready for writing and no other process is currently writting to shared memory.
- Used for allowing to generate only one message at a time using a LLM. If several requests are sent, other requests wait for a semaphore to open.
- Used for writting only one message at the time to the server. If several processes have something to write to the server, this semaphore takes care that only one message will be written at that time.

### Pipes
- Every channel process uses pipes. They receive data from server reader process which identifies by from which channel it was received.
- Response generator process use pipes. It receives data from channel monitor process and then generates a response regarding the prompt given, and sends it back to the channel monitor process to send to the users.
- Admin channel process also uses pipes. It receives data from server reader process with user specified commands and then executes them.

### Shared Memory
- Shared memory is used in several cases. One of them is to monitor if a socket is still alive. If at some point socket breaks bool flag is changed to false. This way the program knows whether this was an intended disconnect or a forced socket disconnection.
- Another use case is to store muted users. Admin channel collects muted users and then later on server reader process can ignore messages sent from those users.
- Shared memory is also used for logging messages. Channel monitor processes write into a shared memory buffer, once that buffer is written semaphore is turned on and is not turned off until the message is written into the file. 

### Signals
- Signals are used to gracefully close the program when a CTRL + Z or a SIGINT signal is received. This approach is used twice, once to stop currently running processes and gracefully exit the program, another time is to gracefully shutdown the program when it is trying to reconnect a socket.
- Signals are also used to wake up logger process when new data in shared memory buffer is available.

## File Structure
### Config
- Stores all config files.
- admin.cfg is a configuration file for creating an admin channel. First line specifies channel name, second line specifies channel password.
- channels.cfg is a configuration channel for joining channels. Channel for joining should be always written in a new line. Program currently supports up to 32 channels.

### Include
- Stores C header files and additional information.
- connection_handler.h stores server ip address, server port and user name for the bot to use.
- message_compilator.h stores OLLAMA model that will be using in the program and a url to generate responses from.

### Logs
- Stores chat.log file. All logging is placed there.

### Responses
- Used for generating OLLAMA responses.

### Src
- C source files

## Custom commands
These commands can be executed in the admin channel.
- ignore user0000. Up to 10 users if this limit is exceeded the chat bot will send a notification. Example ignore kaza5555. Currently user name is expected to not be longer than 9 symbols.
- poweroff - send a command to shutdown the bot without leaving the irc channel.
- More will be added...