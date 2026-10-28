#ifndef SERVER_H
#define SERVER_H

typedef struct Server {
    int id;
    struct Station* station;
} Server;

// Types de requêtes traitées par le serveur
typedef enum {
    REQ_CHARGE_DEMAND,
    REQ_CHARGE_END,
    REQ_ALERT,
    REQ_SHUTDOWN
} RequestType;

void handle_request(RequestType request);
void function_to_handle_charge_demand(void);
void function_to_handle_charge_end(void);
void function_to_handle_alert(void);
void function_to_shutdown_server(void);








#endif // SERVER_H