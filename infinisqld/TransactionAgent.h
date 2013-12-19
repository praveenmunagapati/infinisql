/*
 * Copyright (c) 2013 Mark Travis <mtravis15432+src@gmail.com>
 * All rights reserved. No warranty, explicit or implicit, provided.
 *
 * This file is part of InfiniSQL(tm).
 
 * InfiniSQL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3
 * as published by the Free Software Foundation.
 *
 * InfiniSQL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with InfiniSQL. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file   TransactionAgent.h
 * @author Mark Travis <mtravis15432+src@gmail.com>
 * @date   Tue Dec 17 14:03:09 2013
 * 
 * @brief  Actor which communicates with clients and executes transactions.
 * Coordinates of activities between several other actors.
 */

#ifndef INFINISQLTRANSACTIONAGENT_H
#define INFINISQLTRANSACTIONAGENT_H

#include "gch.h"
#include "Operation.h"
#include "Table.h"
#include "Schema.h"
#include "Larxer.h"

enum transactionpayload_e
{
    NOPAYLOAD = 0,
    SUBTRANSACTIONCMDPAYLOAD,
    COMMITROLLBACKPAYLOAD
};

enum idlcmd_e
{
    IDLNOCMD = 0,
    IDLROWLOCK,
    IDLINDEXLOCK,
    IDLINSERTROW,
    IDLINDEX,
    IDLDELETEUNIQUEINDEX,
    IDLINSERTNONUNIQUEINDEX,
    IDLINSERTNULLINDEX,
    IDLDELETENONUNIQUEINDEX,
    IDLDELETENULLINDEX,
    IDLREPLACEINDEX,
    IDLROLLBACKROW,
    IDLROLLBACKINDEX,
};

// deletethis this
typedef struct
{
    idlcmd_e cmd;
    bool isrow;

    int64_t rowid;
    int64_t tableid;
    std::string row;
    locktype_e locktype;

    // for index
    int64_t fieldid;
    fieldValue_s fieldVal;
    // for pending locks
    int64_t pendingcmdid;
} idl;
// delete this
typedef vector<idl> cmds;

#include "Transaction.h"
#include "Applier.h"

/*
  typedef struct
  {
  int64_t resultCode;
  msgpack::sbuffer *sbuf;
  } procedureResponse;
*/

typedef boost::unordered_map<std::string, procedures_s> domainProceduresMap;

typedef struct
{
    int64_t resultCode;
    msgpack::sbuffer *sbuf;
    std::vector<std::string> *responseVector;
} responseData;

typedef struct
{
    int64_t domainid;
    int64_t userid;
    std::string domainName;
} authInfo;

typedef boost::unordered_map<int, responseData> sendLaterMap;
// there is apparently a bug in boost::unordered_map that causes
// this thing to dump core when trying to count or erase a key under
// some circumstances. no time presently to figure it out
//typedef boost::unordered_map<int, authInfo> socketAuthInfo;
typedef std::map<int, authInfo> socketAuthInfo;
typedef boost::unordered_map<int64_t, class Operation *> operationMap;

msgpack::sbuffer *makeSbuf(msgpack::sbuffer *);
msgpack::sbuffer *makeSbuf(vector<string> *);
msgpack::sbuffer *makeSbuf(std::map<string, string> *);

class TransactionAgent
{
public:
    TransactionAgent(Topology::partitionAddress *myIdentityArg);
    virtual ~TransactionAgent();

    void updateReplicas();

    // pubic for replyTa:
    //  Mbox::msgstruct msgsnd;
    int64_t operationid;
    int64_t domainid;
    int64_t userid;
    int64_t status;
    int64_t tainstance;
    //public for createSchema:
    class Message *msgrcv;
    REUSEMESSAGES
        domainidToSchemaMap domainidsToSchemata;

    // needs to be all/mostly public for stored procedures

    // builtins
    void ping(builtincmds_e cmd);
    void login(builtincmds_e cmd);
    void logout(builtincmds_e cmd);
    void changepassword(builtincmds_e cmd);
    void createdomain(builtincmds_e cmd);
    void createuser(builtincmds_e cmd);
    void deleteuser(builtincmds_e cmd);
    void deletedomain(builtincmds_e cmd);
    void createschema(builtincmds_e cmd);
    void createtable(builtincmds_e cmd);
    void addcolumn(builtincmds_e cmd);
    void deleteindex(builtincmds_e cmd);
    void deletetable(builtincmds_e cmd);
    void deleteschema(builtincmds_e cmd);
    void loadprocedure(builtincmds_e cmd);
    void compile(builtincmds_e cmd);
    void schemaBoilerplate(builtincmds_e cmd, int builtin);
    // loop-back schema commands
    void TAcreateschema();
    void TAcreatetable();
    void TAaddcolumn();
    void TAdeleteindex();
    void TAdeletetable();
    void TAdeleteschema();
    void TAloadprocedure();
    void endOperation();
    //private:
    void endConnection();
    int64_t readSocket();
    int64_t getnexttransactionid();
    int64_t getnextapplierid();
    void badMessageHandler();
    void newprocedure(int64_t entrypoint);
    void newstatement();

    void handledispatch();

    template <typename T>
        void sendResponse(bool resending, int64_t resultCode, T response)
    {
        msgpack::sbuffer *sbuf = makeSbuf(response);
        int64_t totalsize = 2*sizeof(uint64_t) + sbuf->size();
        char payload[PAYLOADSIZE];
        uint64_t x = htobe64((uint64_t)totalsize);
        memcpy(payload, &x, sizeof(x));
        x = htobe64((uint64_t)resultCode);
        memcpy(payload + sizeof(x), &x, sizeof(x));

        memcpy(payload+(2*sizeof(x)), sbuf->data(), sbuf->size());
        ssize_t totalwritten = write(sockfd, payload, totalsize);

        if (totalwritten == totalsize)   // send was successful
        {
            if (resending)
            {
                waitingToSend.erase(sockfd);
            }

            delete sbuf;
            return;
        }

        if (totalwritten == -1)
        {
            if (errno==EAGAIN || errno==EWOULDBLOCK)
            {
                // we wait for a time to send the data
                sendLaterMap::iterator sendLaterIterator;
                sendLaterIterator = waitingToSend.find(sockfd);

                if (sendLaterIterator != waitingToSend.end()) //gratuitous
                {
                    printf("%s %i endConnection\n", __FILE__, __LINE__);
                    endConnection();
                    return;
                }

                if (!resending)
                {
                    responseData resp;
                    resp.resultCode = resultCode;
                    resp.sbuf = sbuf;
                    resp.responseVector = NULL;
                    waitingToSend[sockfd] = resp;
                }

                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
                ev.data.fd = sockfd;
                if (epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &ev))
                {
                    printf("%s %i endConnection\n", __FILE__, __LINE__);
                    endConnection();
                }
            }
            else
            {
                perror("oops");
                endConnection();
            }

            return;
        }
    }

    template < typename T >
        static void usmReply(T actor, Topology::addressStruct &dest,
                             class MessageUserSchema &msg)
    {
        msg.messageStruct.payloadtype = PAYLOADUSERSCHEMA;
        msg.userschemaStruct.operationid = actor->operationid;
        msg.userschemaStruct.domainid = actor->domainid;
        msg.userschemaStruct.userid = actor->userid;
        msg.userschemaStruct.status = actor->status;

        actor->mboxes.toActor(actor->myIdentity.address, dest, msg);
    }

    Topology::partitionAddress myIdentity;
    class Mboxes mboxes;
    class Topology myTopology;
    int64_t instance;
    class Mbox *mymboxPtr;
    int epollfd;
    int sockfd;
    char payload[PAYLOADSIZE];
    std::string *operation;
    socketAuthInfo loggedInUsers;
    int64_t argsize;
    char args[PAYLOADSIZE]; // get rid of this when possible
    std::string argstring;
    sendLaterMap waitingToSend;
    operationMap pendingOperations;
    operationMap::iterator pendingOperationsIterator;
    int64_t operationidcounter; // never touch this!
    std::string domainName;
    std::vector<std::string> responseVector;
    class Operation *operationPtr;
    domainidToSchemaMap::iterator domainidsToSchemataIterator;
    boost::unordered_map<int64_t, domainProceduresMap> domainidsToProcedures;
    boost::unordered_map<int64_t, class Transaction *> Transactions;
    boost::unordered_map<int64_t, class Applier *> Appliers;
    // Pgs[socket] = *Pg
    boost::unordered_map<int, class Pg *> Pgs;
    int64_t nexttransactionid;
    int64_t nextapplierid;
    int batchSendCount;

    size_t myreplica;
    size_t mymember;
    Topology::addressStruct replicaAddress;
    std::vector<Topology::addressStruct> replicaAddresses;

    // statements[domainid][statementname] = compiledstatement
    // user submits statementname
    boost::unordered_map< int64_t,
        boost::unordered_map<std::string, class Statement> > statements;
};

class ApiInterface;
typedef ApiInterface *(*spclasscreate)(class TransactionAgent *,
                                       class ApiInterface *, void *);
typedef void(*spclassdestroy)(ApiInterface *);

template < typename T >
void replyTa(T servent, topic_e result, void *msg)
{
    class MessageUserSchema &msgref = *(class MessageUserSchema *)msg;
    servent->msgsnd.data = (void *)msg;
    ((class Message *)servent->msgsnd.data)->messageStruct.topic = result;
    ((class Message *)servent->msgsnd.data)->messageStruct.payloadtype =
        PAYLOADUSERSCHEMA;
    msgref.userschemaStruct.operationid = servent->operationid;
    msgref.userschemaStruct.domainid = servent->domainid;
    msgref.userschemaStruct.userid = servent->userid;
    msgref.userschemaStruct.status = servent->status;
}

void *transactionAgent(void *);

#endif  /* INFINISQLTRANSACTIONAGENT_H */