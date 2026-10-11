#include <errno.h>
/* One deadline covers request, header and payload, including fragmented replies. */
static bool socket_exact(int sock,uint8_t *data,size_t length,bool sending,int64_t deadline) {
    size_t done=0;
    while(done<length){
        int64_t left=deadline-now_ms();if(left<=0)return false;
        fd_set ready;FD_ZERO(&ready);FD_SET(sock,&ready);
        struct timeval wait={.tv_sec=left/1000,.tv_usec=(left%1000)*1000};
        int selected=select(sock+1,sending?NULL:&ready,sending?&ready:NULL,NULL,&wait);
        if(selected<0 && errno==EINTR)continue;
        if(selected<=0)return false;
        int count=sending?send(sock,data+done,length-done,0):recv(sock,data+done,length-done,0);
        if(count<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
        if(count<=0)return false;
        done+=(size_t)count;
    }
    return true;
}
