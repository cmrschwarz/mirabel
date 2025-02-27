#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "SDL_net.h"
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "mirabel/event_queue.h"
#include "mirabel/event.h"
#include "network/util.hpp"

#include "network/network_server.hpp"

namespace Network {

    NetworkServer::NetworkServer()
    {
        event_queue_create(&send_queue);

        server_socketset = SDLNet_AllocSocketSet(1);
        if (server_socketset == NULL) {
            printf("[ERROR] failed to allocate server socketset\n");
        }
        client_connections = (connection*)malloc(client_connection_bucket_size * sizeof(connection));
        if (client_connections == NULL) {
            printf("[ERROR] failed to allocate client connections bucket\n");
        }
        for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
            client_connections[i] = connection();
        }
        client_socketset = SDLNet_AllocSocketSet(client_connection_bucket_size);
        if (client_socketset == NULL) {
            printf("[ERROR] failed to allocate client socketset\n");
        }
        ssl_ctx = util_ssl_ctx_init(UTIL_SSL_CTX_TYPE_SERVER, "./server-fullchain.pem", "./server-privkey.pem"); //TODO dont hardcode cert names
        if (ssl_ctx == NULL) {
            printf("[ERROR] failed to init ssl ctx\n");
        }
    }

    NetworkServer::~NetworkServer()
    {
        util_ssl_ctx_free(ssl_ctx);
        SDLNet_FreeSocketSet(client_socketset);
        free(client_connections);
        SDLNet_FreeSocketSet(server_socketset);

        event_queue_destroy(&send_queue);
    }

    bool NetworkServer::open(const char* host_address, uint16_t host_port)
    {
        if (server_socketset == NULL || client_connections == NULL || client_socketset == NULL) {
            printf("[ERROR] server construction failure\n");
            return false;
        }
        if (SDLNet_ResolveHost(&server_ip, NULL, host_port) != 0) {
            printf("[ERROR] could not resolve server address\n");
            return false;
        }
        server_socket = SDLNet_TCP_Open(&server_ip);
        if (server_socket == NULL) {
            printf("[ERROR] server socket failed to open\n");
            return false;
        }
        SDLNet_TCP_AddSocket(server_socketset, server_socket); // cant fail, we only have one socket for our size 1 set
        server_runner = std::thread(&NetworkServer::server_loop, this); // socket open, start server_runner
        send_runner = std::thread(&NetworkServer::send_loop, this); // socket open, start send_runner
        recv_runner = std::thread(&NetworkServer::recv_loop, this); // socket open, start recv_runner
        return true;
    }

    void NetworkServer::close()
    {
        // stop server_runner
        SDLNet_TCP_DelSocket(server_socketset, server_socket);
        SDLNet_TCP_Close(server_socket);
        server_socket = NULL;
        event_any es;
        event_create_type(&es, EVENT_TYPE_EXIT); // stop send_runner
        event_queue_push(&send_queue, &es);
        // stop recv_runner
        for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
            TCPsocket* client_socket = &(client_connections[i].socket);
            SDLNet_TCP_DelSocket(client_socketset, *client_socket);
            SDLNet_TCP_Close(*client_socket);
            *client_socket = NULL;
            util_ssl_session_free(&(client_connections[i]));
        }
        // everything closed, join dead runners
        server_runner.join();
        send_runner.join();
        recv_runner.join();
    }

    void NetworkServer::server_loop()
    {
        uint32_t buffer_size = 16384;
        uint8_t* data_buffer = (uint8_t*)malloc(buffer_size); // recycled buffer for data
        uint32_t* db_event_type = reinterpret_cast<uint32_t*>(data_buffer);

        while (server_socket != NULL) {
            int ready = SDLNet_CheckSockets(server_socketset, 15); //TODO should be UINT32_MAX, but then it doesnt exit on self socket close
            //TODO if we have to go out of waiting anyway every once in a while, the maybe check a dedicated heartbeat inbox here too?
            if (ready == -1) {
                break;
            }
            // handle new connection
            if (!SDLNet_SocketReady(server_socket)) {
                continue;
            }
            printf("[----] = socket is ready\n");
            TCPsocket incoming_socket;
            incoming_socket = SDLNet_TCP_Accept(server_socket);
            if (incoming_socket == NULL) {
                printf("[ERROR] = server socket closed unexpectedly\n");
                break;
            }
            printf("[----] = processing incoming connection\n");
            // check if there is still space for a new client connection
            connection* connection_slot = NULL;
            uint32_t connection_id = EVENT_CLIENT_NONE;
            for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
                if (client_connections[i].socket == NULL) {
                    connection_slot = &(client_connections[i]);
                    connection_id = i + 1;
                    break;
                }
            }
            if (connection_slot == NULL) {
                // no slot available for new client connection, drop it
                *db_event_type = EVENT_TYPE_NETWORK_PROTOCOL_NOK;
                int send_len = sizeof(event);
                int sent_len = SDLNet_TCP_Send(incoming_socket, data_buffer, sizeof(event));
                if (sent_len != send_len) {
                    printf("[WARN] = packet sending failed\n");
                }
                SDLNet_TCP_Close(incoming_socket);
                printf("[INFO] = refused new connection\n");
            } else {
                // slot available for new client, accept it
                // send protocol client id set, functions as ok if set as initial
                *db_event_type = EVENT_TYPE_NETWORK_PROTOCOL_CLIENT_ID_SET;
                *(db_event_type + 1) = connection_id;
                int send_len = sizeof(event);
                int sent_len = SDLNet_TCP_Send(incoming_socket, data_buffer, sizeof(event));
                if (sent_len != send_len) {
                    printf("[WARN] = packet sending failed\n");
                }
                connection_slot->state = PROTOCOL_CONNECTION_STATE_NONE;
                connection_slot->socket = incoming_socket;
                connection_slot->peer_addr = *SDLNet_TCP_GetPeerAddress(incoming_socket);
                connection_slot->client_id = connection_id;
                util_ssl_session_init(ssl_ctx, connection_slot, UTIL_SSL_CTX_TYPE_SERVER);
                SDLNet_TCP_AddSocket(client_socketset, connection_slot->socket);
                printf("[INFO] = new connection initializing, client id %d\n", connection_id);
                SSL_do_handshake(connection_slot->ssl_session);
                //TODO better error handling and at more places
                unsigned long ev = ERR_get_error();
                while (ev != 0) {
                    printf("[ERROR] %s\n", ERR_error_string(ev, NULL));
                    ev = ERR_get_error();
                }
            }
        }

        free(data_buffer);
        // if server_loop closes, notify server so it can handle it
        event_any es;
        event_create_type(&es, EVENT_TYPE_NETWORK_ADAPTER_SOCKET_CLOSED);
        event_queue_push(recv_queue, &es);
    }

    void NetworkServer::send_loop()
    {
        uint32_t base_buffer_size = 16384;
        uint8_t* data_buffer_base = (uint8_t*)malloc(base_buffer_size); // recycled buffer for outgoing data

        // wait until event available
        bool quit = false;
        while (!quit) {
            event_any e;
            event_queue_pop(&send_queue, &e, UINT32_MAX);
            connection* target_client = NULL; // target client to use for processing this event
            switch (e.base.type) {
                case EVENT_TYPE_NULL: {
                    printf("[WARN] > received impossible null event\n");
                } break;
                case EVENT_TYPE_EXIT: {
                    quit = true;
                    break;
                } break;
                //TODO heartbeat
                default: {
                    // find target client connection to send to
                    //TODO use client id as index into the bucket, give every bucket a base offset
                    for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
                        if (client_connections[i].client_id == e.base.client_id) {
                            target_client = &(client_connections[i]);
                            break;
                        }
                    }
                    if (target_client == NULL) {
                        printf("[WARN] > failed to find connection for sending event, discarded %lu bytes\n", event_size(&e));
                        break;
                    }
                    if (target_client->state != PROTOCOL_CONNECTION_STATE_ACCEPTED) {
                        switch (target_client->state) {
                            case PROTOCOL_CONNECTION_STATE_PRECLOSE: {
                                printf("[WARN] > SECURITY: outgoing event %d on pre-closed connection dropped\n", e.base.type);
                            } break;
                            case PROTOCOL_CONNECTION_STATE_NONE:
                            case PROTOCOL_CONNECTION_STATE_INITIALIZING: {
                                printf("[WARN] > SECURITY: outgoing event %d on unsecured connection dropped\n", e.base.type);
                            } break;
                            default:
                            case PROTOCOL_CONNECTION_STATE_WARNHELD: {
                                printf("[WARN] > SECURITY: outgoing event %d on unaccepted connection dropped\n", e.base.type);
                            } break;
                        }
                        break;
                    }
                    // universal event->packet encoding, for POD events
                    uint8_t* data_buffer = data_buffer_base;
                    int write_len = event_size(&e);
                    if (write_len > base_buffer_size) {
                        data_buffer = (uint8_t*)malloc(write_len);
                    }
                    event_serialize(&e, data_buffer);
                    int wrote_len = SSL_write(target_client->ssl_session, data_buffer, write_len);
                    if (wrote_len != write_len) {
                        printf("[WARN] > ssl write failed\n");
                    } else {
                        printf("[----] > ssl wrote event, type %d, len %d\n", e.base.type, write_len);
                    }
                    if (data_buffer != data_buffer_base) {
                        free(data_buffer);
                    }
                } /* fallthrough */
                case EVENT_TYPE_NETWORK_INTERNAL_SSL_WRITE: {
                    // either ssl wants to write, but we dont have anything to send to trigger this ourselves
                    // or fallthrough from event send ssl write, in any case just send forward ssl->tcp

                    // if this is not a fallthrough we need to find the target client
                    if (target_client == NULL) {
                        // find target client connection to send to
                        //TODO use client id as index into the bucket, give every bucket a base offset
                        for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
                            if (client_connections[i].client_id == e.base.client_id) {
                                target_client = &(client_connections[i]);
                                break;
                            }
                        }
                        if (target_client == NULL) {
                            printf("[WARN] > failed to find connection %d for sending ssl write\n", e.base.client_id);
                            break;
                        }
                    }

                    while (true) {
                        int pend_len = BIO_ctrl_pending(target_client->send_bio);
                        printf("[----] > pending to send: %i bytes\n", pend_len);
                        if (pend_len == 0) {
                            // nothing pending to send
                            break;
                        }
                        int send_len = BIO_read(target_client->send_bio, data_buffer_base, base_buffer_size);
                        printf("[----] > ssl outputs %i bytes for sending\n", send_len);
                        if (send_len == 0) {
                            // empty read, can this happen?
                            break;
                        }
                        int sent_len = SDLNet_TCP_Send(target_client->socket, data_buffer_base, send_len);
                        if (sent_len != send_len) {
                            printf("[WARN] > packet sending failed\n");
                        } else {
                            printf("[----] > sent %d bytes of data to client id %d\n", sent_len, e.base.client_id);
                        }
                    }
                } break;
            }
        }

        free(data_buffer_base);
    }

    void NetworkServer::recv_loop()
    {
        uint32_t buffer_size = 16384;
        uint8_t* data_buffer = (uint8_t*)malloc(buffer_size); // recycled buffer for incoming data

        while (true) {
            int ready = SDLNet_CheckSockets(client_socketset, 15); //TODO should be UINT32_MAX, but then it doesnt exit on self socket close
            //TODO if we have to go out of waiting anyway every once in a while, the maybe check a dedicated heartbeat inbox here too?
            if (ready == -1) {
                break;
            }
            connection* ready_client = NULL;
            for (uint32_t i = 0; i < client_connection_bucket_size; i++) {
                //TODO traverse clients in order of activity
                if (ready <= 0) {
                    break; // exit search for ready clients early if we already served the ready count
                }
                if (!SDLNet_SocketReady(client_connections[i].socket)) {
                    continue;
                }
                printf("[----] < socket for client id %d is ready\n", client_connections[i].client_id);
                ready--;
                ready_client = &(client_connections[i]);
                // handle data for the ready_client
                int recv_len = SDLNet_TCP_Recv(ready_client->socket, data_buffer, buffer_size);
                if (recv_len <= 0) {
                    // connection closed
                    SDLNet_TCP_DelSocket(client_socketset, ready_client->socket);
                    SDLNet_TCP_Close(ready_client->socket);
                    switch (ready_client->state) {
                        default:
                        case PROTOCOL_CONNECTION_STATE_NONE: {
                            printf("[WARN] < client id %d connection closed before initialization\n", ready_client->client_id);
                        } break;
                        case PROTOCOL_CONNECTION_STATE_INITIALIZING: {
                            printf("[WARN] < client id %d connection closed while initializing\n", ready_client->client_id);
                        } break;
                        case PROTOCOL_CONNECTION_STATE_WARNHELD:
                        case PROTOCOL_CONNECTION_STATE_ACCEPTED: // both closed unexpectedly
                        case PROTOCOL_CONNECTION_STATE_PRECLOSE: {
                            // pass, everything fine
                            if (ready_client->state == PROTOCOL_CONNECTION_STATE_PRECLOSE) {
                                printf("[INFO] < client id %d connection closed\n", ready_client->client_id);
                            } else {
                                printf("[WARN] < client id %d connection closed unexpectedly\n", ready_client->client_id);
                            }
                            event_any es;
                            event_create_type_client(&es, EVENT_TYPE_NETWORK_ADAPTER_CLIENT_DISCONNECTED, ready_client->client_id);
                            event_queue_push(recv_queue, &es);
                        } break;
                    }
                    util_ssl_session_free(ready_client);
                    ready_client->reset(); // sets everything 0/NULL/NONE
                    continue;
                }
                printf("[----] < tcp received %i bytes\n", recv_len);

                if (ready_client->state == PROTOCOL_CONNECTION_STATE_PRECLOSE) {
                    printf("[WARN] < discarding %d recv bytes on pre-closed connection\n", recv_len);
                    continue;
                }

                if (ready_client->state == PROTOCOL_CONNECTION_STATE_NONE) {
                    // first client response after connection established
                    printf("[----] < client id %d connection initializing\n", ready_client->client_id);
                    ready_client->state = PROTOCOL_CONNECTION_STATE_INITIALIZING;
                }

                // forward tcp->ssl
                // if our buffer is to small, the rest of the data will show up as a ready socket again, then we read it in the next round
                BIO_write(ready_client->recv_bio, data_buffer, recv_len);
                printf("[----] < ssl bio ingested %i bytes\n", recv_len);
                // if ssl is still doing internal things, don't bother
                if (ready_client->state == PROTOCOL_CONNECTION_STATE_INITIALIZING) {
                    if (!SSL_is_init_finished(ready_client->ssl_session)) {
                        SSL_do_handshake(ready_client->ssl_session);
                        //TODO better error handling and at more places
                        unsigned long ev = ERR_get_error();
                        while (ev != 0) {
                            printf("[ERROR] %s\n", ERR_error_string(ev, NULL));
                            ev = ERR_get_error();
                        }
                        // queue generic want write, just in case ssl may want to write
                        event_any es;
                        event_create_type_client(&es, EVENT_TYPE_NETWORK_INTERNAL_SSL_WRITE, ready_client->client_id);
                        event_queue_push(&send_queue, &es);
                        printf("[----] < ssl handshake progressed + internal ssl write\n");
                        if (!SSL_is_init_finished(ready_client->ssl_session)) {
                            continue;
                        }
                    }
                    // handshake is finished, promote connection state if possible
                    // no verification necessary on server side
                    ready_client->state = PROTOCOL_CONNECTION_STATE_ACCEPTED;
                    printf("[INFO] < client %d connection accepted\n", ready_client->client_id);
                    //REWORK this never reaches the client at the right point in time, it is sent before the adapter is installed
                    // somehow make sure we only USE the client when has authenticated, i.e. installed its adapter
                    event_any es;
                    event_create_type_client(&es, EVENT_TYPE_NETWORK_ADAPTER_CLIENT_CONNECTED, ready_client->client_id); // inform server that client is connected and ready to use
                    event_queue_push(recv_queue, &es);
                }

                // PROTOCOL_CONNECTION_STATE_WARNHELD, server never uses this

                while (true) {
                    event_any recv_event;
                    uint8_t size_peek[sizeof(size_t)];
                    int im_rd = SSL_peek(ready_client->ssl_session, size_peek, sizeof(size_t));
                    if (im_rd == 0) {
                        // empty ssl read do nothing
                        break;
                    }
                    if (im_rd < sizeof(size_t)) {
                        printf("[WARN] < %d invalid packet length bytes received from client id %d\n", im_rd, ready_client->client_id);
                        break;
                    }
                    size_t event_size = event_read_size(size_peek);
                    if (ready_client->fragment_buf != NULL || event_size > buffer_size) {
                        // existing fragment, or does not fit the buffer, handled the same
                        if (ready_client->fragment_buf == NULL) {
                            ready_client->fragment_buf = malloc(event_size);
                            ready_client->fragment_size_target = event_size;
                            printf("[----] < event packet announced with %zu byte size, client id %d\n", event_size, ready_client->client_id);
                        } else {
                            printf("[----] < incoming additional bytes for existing fragment, client id %d\n", ready_client->client_id);
                        }
                        while (ready_client->fragment_size < ready_client->fragment_size_target) {
                            // need to do multiple reads, to catch multiple potential ssl records (16kB)
                            im_rd = SSL_read(ready_client->ssl_session, (char*)ready_client->fragment_buf + ready_client->fragment_size, ready_client->fragment_size_target - ready_client->fragment_size);
                            printf("[----] < ssl outputs %i bytes, client id %d\n", im_rd, ready_client->client_id);
                            if (im_rd == 0) {
                                break;
                            }
                            ready_client->fragment_size += im_rd;
                        }
                        if (ready_client->fragment_size < ready_client->fragment_size_target) {
                            // fragment still incomplete
                            break;
                        }
                        event_deserialize(&recv_event, ready_client->fragment_buf, (char*)ready_client->fragment_buf + event_size);
                        // reset fragment buffer state
                        ready_client->fragment_size_target = 0;
                        ready_client->fragment_size = 0;
                        free(ready_client->fragment_buf);
                        ready_client->fragment_buf = NULL;
                    } else {
                        printf("[----] < event packet announced with %zu byte size, client id %d\n", event_size, ready_client->client_id);
                        // not a frag and fits entirely into data_buffer
                        size_t buffer_fill = 0;
                        while (buffer_fill < event_size) {
                            // need to do multiple reads, to catch multiple potential ssl records (16kB)
                            im_rd = SSL_read(ready_client->ssl_session, data_buffer + buffer_fill, event_size - buffer_fill);
                            printf("[----] < ssl outputs %i bytes, client id %d\n", im_rd, ready_client->client_id);
                            if (im_rd == 0) {
                                break;
                            }
                            buffer_fill += im_rd;
                        }
                        if (buffer_fill < event_size) {
                            printf("[WARN] < discarding %zu unusable bytes of received data, client id %d\n", buffer_fill, ready_client->client_id);
                        }
                        event_deserialize(&recv_event, data_buffer, (char*)data_buffer + event_size);
                    }
                    if (recv_event.base.type == EVENT_TYPE_NULL) {
                        printf("[WARN] < event packet deserialization error, client id %d\n", ready_client->client_id);
                    }
                    if (recv_event.base.client_id != ready_client->client_id) {
                        printf("[WARN] < client id %d provided wrong id %d in incoming packet\n", ready_client->client_id, recv_event.base.client_id);
                        recv_event.base.client_id = ready_client->client_id;
                    }
                    // switch on type
                    switch (recv_event.base.type) {
                        case EVENT_TYPE_NULL:
                            break; // drop null events
                        case EVENT_TYPE_NETWORK_PROTOCOL_DISCONNECT: {
                            //REWORK need more?
                            ready_client->state = PROTOCOL_CONNECTION_STATE_PRECLOSE;
                        } break;
                        case EVENT_TYPE_NETWORK_PROTOCOL_PING: {
                            printf("[INFO] < ping from client sending pong\n");
                            event_any es;
                            event_create_type_client(&es, EVENT_TYPE_NETWORK_PROTOCOL_PONG, recv_event.base.client_id);
                            event_queue_push(&send_queue, &es);
                        } break;
                        default: {
                            printf("[----] < received event from client id %d, type: %d\n", ready_client->client_id, recv_event.base.type);
                            event_queue_push(recv_queue, &recv_event);
                        } break;
                    }
                }

                // loop into ready check on next client connection
            }

            // loop into next wait on socketset
        }

        free(data_buffer);
        // if server_loop closes, notify server so it can handle it
        event_any es;
        event_create_type(&es, EVENT_TYPE_NETWORK_ADAPTER_SOCKET_CLOSED);
        event_queue_push(recv_queue, &es);
    }

} // namespace Network
