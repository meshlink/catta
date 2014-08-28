#include "wincompat.h"
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

// helper: convert WSAGetLastError() to an errno constant
static int wsa_errno(void)
{
    switch(WSAGetLastError()) {
        case WSAECONNRESET:     return ECONNRESET;
        case WSAEFAULT:         return EFAULT;
        case WSAEINPROGRESS:    return EINPROGRESS;
        case WSAEINTR:          return EINTR;
        case WSAEINVAL:         return EINVAL;
        case WSAEMSGSIZE:       return EMSGSIZE;
        case WSAENETDOWN:       return ENETDOWN;
        case WSAENETRESET:      return ENETRESET;
        case WSAENOTCONN:       return ENOTCONN;
        case WSAENOTSOCK:       return ENOTSOCK;
        case WSAEOPNOTSUPP:     return EOPNOTSUPP;
        case WSAEWOULDBLOCK:    return EWOULDBLOCK;
        default:
            return EINVAL;
    }
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    LPFN_WSARECVMSG WSARecvMsg = NULL;
    GUID wsaid = WSAID_WSARECVMSG;
    DWORD b;

    DWORD bytesrcvd;
    WSAMSG wsamsg;
    size_t i;
    int r;

    // size_t is larger than DWORD on 64bit
    if(msg->msg_iovlen > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    // obtain the function pointer to WSARecvMsg
    r = WSAIoctl(sockfd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &wsaid, sizeof(wsaid), &WSARecvMsg, sizeof(WSARecvMsg),
                 &b, NULL, NULL);
    if(r == SOCKET_ERROR) {
        errno = wsa_errno();
        return -1;
    }
    assert(b == sizeof(WSARecvMsg));
    assert(WSARecvMsg != NULL);

    // convert msghdr to WSAMSG structure
    wsamsg.name = msg->msg_name;
    wsamsg.namelen = msg->msg_namelen;
    wsamsg.lpBuffers = malloc(msg->msg_iovlen * sizeof(WSABUF));
    wsamsg.dwBufferCount = msg->msg_iovlen;
    wsamsg.Control.len = msg->msg_controllen;
    wsamsg.Control.buf = msg->msg_control;
    wsamsg.dwFlags = (DWORD)flags;

    // all flags that fit into dwFlags also fit through the flags argument
    assert(sizeof(DWORD) <= sizeof(flags));

    if(wsamsg.lpBuffers == NULL) {
        // malloc will have set errno
        return -1;
    }

    // re-wrap iovecs as WSABUFs
    for(i=0; i<msg->msg_iovlen; i++) {
        // size_t vs. u_long
        if(msg->msg_iov[i].iov_len > ULONG_MAX) {
            free(wsamsg.lpBuffers);
            errno = EINVAL;
            return -1;
        }

        wsamsg.lpBuffers[i].len = msg->msg_iov[i].iov_len;
        wsamsg.lpBuffers[i].buf = msg->msg_iov[i].iov_base;
    }

    r = WSARecvMsg(sockfd, &wsamsg, &bytesrcvd, NULL, NULL);

    // the allocated WSABUF wrappers are no longer needed
    free(wsamsg.lpBuffers);

    if(r == SOCKET_ERROR) {
        // XXX do we need special handling for ENETRESET, EMSGSIZE, ETIMEDOUT?
        errno = wsa_errno();
        return -1;
    }

    // DWORD has one bit more than ssize_t on 32bit
    if(bytesrcvd > SSIZE_MAX) {
        errno = EINVAL;
        return -1;
    }

    // transfer results from wsamsg to msg
    // NB: the data and control buffers are shared
    msg->msg_controllen = wsamsg.Control.len;
    msg->msg_flags = (int)wsamsg.dwFlags;
        // all flags that fit into dwFlags also fit into msg_flags (see above)

    return bytesrcvd;
}

int uname(struct utsname *buf)
{
    SYSTEM_INFO si;
    const char *arch = "unknown";

    memset(buf, 0, sizeof(struct utsname));

    // operating system
    strncpy(buf->sysname, "Windows", sizeof(buf->sysname)-1);
    strncpy(buf->release, "unknown", sizeof(buf->sysname)-1);   // we don't need it
    strncpy(buf->version, "unknown", sizeof(buf->sysname)-1);   // we don't need it

    // computer (node) name
    if(GetComputerName(buf->nodename, sizeof(buf->nodename)-1) == 0) {
        errno = EFAULT;
        return -1;
    }

    // hardware type
    GetSystemInfo(&si);
    switch(si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "amd64"; break;
        case PROCESSOR_ARCHITECTURE_ARM:   arch = "arm";   break;
        case PROCESSOR_ARCHITECTURE_IA64:  arch = "ia64";  break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86";   break;
        default: arch = "unknown";
    }
    strncpy(buf->machine, arch, sizeof(buf->machine)-1);

    return 0;
}
