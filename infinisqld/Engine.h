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
 * @file   Engine.h
 * @author Mark Travis <mtravis15432+src@gmail.com>
 * @date   Tue Dec 17 13:09:12 2013
 * 
 * @brief  Engine actors store and manipulate data. Each engine interacts with
 * a single partition, and vice versa.
 */

#ifndef INFINISQLENGINE_H
#define INFINISQLENGINE_H

#include "gch.h"
#include "Table.h"
#include "TransactionAgent.h"
#include "SubTransaction.h"

/** 
 * @brief Engine actor. Each Engine corresponds to a data partition.
 */
class Engine
{
public:
    struct background_s
    {
        int64_t applierid;
        Topology::addressStruct taAddress;

        std::vector<MessageDispatch::record_s> rows;
        std::vector<MessageApply::applyindex_s> indices;
    };

    Engine(Topology::partitionAddress *myIdentityArg);
    virtual ~Engine();

    bool applyItem(int64_t subtransactionid, class Schema &schemaRef,
                   MessageDispatch::record_s &record);
    bool applyItem(int64_t subtransactionid, class Schema &schemaRef,
                   MessageApply::applyindex_s &indexinfo);

    friend class SubTransaction;

    // public for replyTa:
    class Message *msgsnd;
    int64_t operationid;
    int64_t domainid;
    int64_t userid;
    int64_t status;
    Topology::addressStruct taAddr;
    //public for createSchema:
    class Message *msgrcv;
    REUSEMESSAGES
        domainidToSchemaMap domainidsToSchemata;
    class Mboxes mboxes;
    Topology::partitionAddress myIdentity;
    int64_t partitionid;

private:
    int64_t getnextsubtransactionid();
    void createschema();
    void createtable();
    void addcolumn();
    void deleteindex();
    void deletetable();
    void deleteschema();
    void getMyPartitionid();
    void apply();
    void background(class MessageApply &inmsg, MessageDispatch::record_s &item);
    void background(class MessageApply &inmsg, MessageApply::applyindex_s &item);

    class Topology myTopology;

    class Mbox *mymboxPtr;
    int64_t argsize;
    int64_t nextsubtransactionid;
    int64_t instance;
    boost::unordered_map<int64_t, class SubTransaction *> SubTransactions;
    std::map<int64_t, background_s> backgrounded;
};

void *engine(void *identity);

#endif  /* INFINISQLENGINE_H */