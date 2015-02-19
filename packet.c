#define SIZE 48
	
typedef enum type {REQ, ACK, DATA, FIN};

struct packet
{

    enum type packet_type;
    int seq_no;
    int length;
    char data[SIZE];
};
