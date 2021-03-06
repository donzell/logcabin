/* Copyright (c) 2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <gtest/gtest.h>

#include "build/Protocol/Raft.pb.h"
#include "Core/ProtoBuf.h"
#include "Core/StringUtil.h"
#include "Core/Time.h"
#include "Protocol/Common.h"
#include "RPC/ServiceMock.h"
#include "RPC/Server.h"
#include "Server/RaftConsensus.h"
#include "Server/Globals.h"

namespace LogCabin {
namespace Server {
namespace {

using namespace RaftConsensusInternal; // NOLINT
typedef RaftConsensus::State State;
typedef RaftConsensus::ClientResult ClientResult;
using std::chrono::milliseconds;

// class Server: nothing to test

// class LocalServer: nothing to test

// class Peer: TODO(ongaro): low-priority tests
// see also regression_nextIndexForNewServer

// class SimpleConfiguration

bool
idHeart(Server& server)
{
    return server.serverId < 3;
}

void
setAddr(Server& server)
{
    using Core::StringUtil::format;
    server.address = format("server%lu", server.serverId);
}

uint64_t
getServerId(Server& server)
{
    return server.serverId;
}

Protocol::Raft::Configuration desc(const std::string& description) {
    using Core::ProtoBuf::fromString;
    return fromString<Protocol::Raft::Configuration>(description);
}

Protocol::Raft::SimpleConfiguration sdesc(const std::string& description) {
    using Core::ProtoBuf::fromString;
    return fromString<Protocol::Raft::SimpleConfiguration>(description);
}

TimePoint round(TimePoint x) {
    milliseconds msSinceEpoch = std::chrono::duration_cast<milliseconds>(
                                                        x.time_since_epoch());
    return TimePoint(msSinceEpoch);
}

class ServerRaftConsensusSimpleConfigurationTest : public ::testing::Test {
    ServerRaftConsensusSimpleConfigurationTest()
        : globals()
        , consensus(globals)
        , cfg()
        , emptyCfg()
        , oneCfg()
    {
        consensus.log.reset(new Log());
        startThreads = false;
        cfg.servers = {
            makeServer(1),
            makeServer(2),
            makeServer(3),
        };
        oneCfg.servers = {
            makeServer(1),
        };
    }
    ~ServerRaftConsensusSimpleConfigurationTest()
    {
        startThreads = true;
    }

    std::shared_ptr<Server> makeServer(uint64_t serverId) {
        return std::shared_ptr<Server>(new Peer(serverId, consensus));
    }

    Globals globals;
    RaftConsensus consensus;
    Configuration::SimpleConfiguration cfg;
    Configuration::SimpleConfiguration emptyCfg;
    Configuration::SimpleConfiguration oneCfg;
};

TEST_F(ServerRaftConsensusSimpleConfigurationTest, all) {
    EXPECT_TRUE(emptyCfg.all(idHeart));
    EXPECT_FALSE(cfg.all(idHeart));
    cfg.servers.pop_back();
    EXPECT_TRUE(cfg.all(idHeart));
}

TEST_F(ServerRaftConsensusSimpleConfigurationTest, contains) {
    std::shared_ptr<Server> s = cfg.servers.back();
    EXPECT_FALSE(emptyCfg.contains(s));
    EXPECT_TRUE(cfg.contains(s));
    cfg.servers.pop_back();
    EXPECT_FALSE(cfg.contains(s));
}


TEST_F(ServerRaftConsensusSimpleConfigurationTest, forEach) {
    cfg.forEach(setAddr);
    emptyCfg.forEach(setAddr);
    EXPECT_EQ("server1", cfg.servers.at(0)->address);
    EXPECT_EQ("server2", cfg.servers.at(1)->address);
    EXPECT_EQ("server3", cfg.servers.at(2)->address);
}

TEST_F(ServerRaftConsensusSimpleConfigurationTest, min) {
    EXPECT_EQ(0U, emptyCfg.min(getServerId));
    EXPECT_EQ(1U, oneCfg.min(getServerId));
    EXPECT_EQ(1U, cfg.min(getServerId));
}

TEST_F(ServerRaftConsensusSimpleConfigurationTest, quorumAll) {
    EXPECT_TRUE(emptyCfg.quorumAll(idHeart));
    EXPECT_TRUE(oneCfg.all(idHeart));
    EXPECT_TRUE(cfg.quorumAll(idHeart));
    cfg.servers.push_back(makeServer(4));
    EXPECT_FALSE(cfg.quorumAll(idHeart));
}

TEST_F(ServerRaftConsensusSimpleConfigurationTest, quorumMin) {
    EXPECT_EQ(0U, emptyCfg.quorumMin(getServerId));
    EXPECT_EQ(1U, oneCfg.quorumMin(getServerId));
    EXPECT_EQ(2U, cfg.quorumMin(getServerId));
    cfg.servers.pop_back();
    EXPECT_EQ(1U, cfg.quorumMin(getServerId));
}

class ServerRaftConsensusConfigurationTest
            : public ServerRaftConsensusSimpleConfigurationTest {
    ServerRaftConsensusConfigurationTest()
        : cfg(1, consensus)
    {
    }
    Configuration cfg;
};

TEST_F(ServerRaftConsensusConfigurationTest, forEach) {
    cfg.forEach(setAddr);
    EXPECT_EQ("server1", cfg.localServer->address);
}

TEST_F(ServerRaftConsensusConfigurationTest, hasVote) {
    auto s2 = makeServer(2);
    EXPECT_FALSE(cfg.hasVote(cfg.localServer));
    EXPECT_FALSE(cfg.hasVote(s2));
    cfg.oldServers.servers.push_back(cfg.localServer);
    cfg.newServers.servers.push_back(s2);
    cfg.state = Configuration::State::STABLE;
    EXPECT_TRUE(cfg.hasVote(cfg.localServer));
    EXPECT_FALSE(cfg.hasVote(s2));
    cfg.state = Configuration::State::TRANSITIONAL;
    EXPECT_TRUE(cfg.hasVote(cfg.localServer));
    EXPECT_TRUE(cfg.hasVote(s2));
    cfg.state = Configuration::State::STAGING;
    EXPECT_TRUE(cfg.hasVote(cfg.localServer));
    EXPECT_FALSE(cfg.hasVote(s2));
}

TEST_F(ServerRaftConsensusConfigurationTest, quorumAll) {
    // TODO(ongaro): low-priority test
}

TEST_F(ServerRaftConsensusConfigurationTest, quorumMin) {
    // TODO(ongaro): low-priority test
}

// resetStagingServers tested at bottom of setStagingServers test

const char* d =
    "prev_configuration {"
    "    servers { server_id: 1, address: '127.0.0.1:61023' }"
    "}";

const char* d2 =
    "prev_configuration {"
    "    servers { server_id: 1, address: '127.0.0.1:61023' }"
    "}"
    "next_configuration {"
        "servers { server_id: 1, address: '127.0.0.1:61025' }"
    "}";

const char* d3 =
    "prev_configuration {"
    "    servers { server_id: 1, address: '127.0.0.1:61023' }"
    "    servers { server_id: 2, address: '127.0.0.1:61024' }"
    "}";

const char* d4 =
    "prev_configuration {"
    "    servers { server_id: 1, address: '127.0.0.1:61023' }"
    "}"
    "next_configuration {"
        "servers { server_id: 2, address: '127.0.0.1:61024' }"
    "}";

TEST_F(ServerRaftConsensusConfigurationTest, setConfiguration) {
    cfg.setConfiguration(1, desc(d));
    EXPECT_EQ(Configuration::State::STABLE, cfg.state);
    EXPECT_EQ(1U, cfg.id);
    EXPECT_EQ(d, cfg.description);
    EXPECT_EQ(1U, cfg.oldServers.servers.size());
    EXPECT_EQ(0U, cfg.newServers.servers.size());
    EXPECT_EQ("127.0.0.1:61023", cfg.oldServers.servers.at(0)->address);
    EXPECT_EQ(1U, cfg.knownServers.size());

    cfg.setConfiguration(2, desc(d2));
    EXPECT_EQ(Configuration::State::TRANSITIONAL, cfg.state);
    EXPECT_EQ(2U, cfg.id);
    EXPECT_EQ(d2, cfg.description);
    EXPECT_EQ(1U, cfg.oldServers.servers.size());
    EXPECT_EQ(1U, cfg.newServers.servers.size());
    EXPECT_EQ("127.0.0.1:61025", cfg.oldServers.servers.at(0)->address);
    EXPECT_EQ("127.0.0.1:61025", cfg.newServers.servers.at(0)->address);
    EXPECT_EQ(1U, cfg.knownServers.size());
}

TEST_F(ServerRaftConsensusConfigurationTest, setStagingServers) {
    cfg.setConfiguration(1, desc(
        "prev_configuration {"
        "    servers { server_id: 1, address: '127.0.0.1:61023' }"
        "}"));
    cfg.setStagingServers(sdesc(
        "servers { server_id: 1, address: '127.0.0.1:61025' }"
        "servers { server_id: 2, address: '127.0.0.1:61027' }"));
    EXPECT_EQ(Configuration::State::STAGING, cfg.state);
    EXPECT_EQ(2U, cfg.newServers.servers.size());
    EXPECT_EQ(1U, cfg.newServers.servers.at(0)->serverId);
    EXPECT_EQ(2U, cfg.newServers.servers.at(1)->serverId);
    EXPECT_EQ("127.0.0.1:61025", cfg.newServers.servers.at(0)->address);
    EXPECT_EQ("127.0.0.1:61027", cfg.newServers.servers.at(1)->address);
    EXPECT_EQ(cfg.localServer, cfg.newServers.servers.at(0));

    cfg.resetStagingServers();
    EXPECT_EQ(Configuration::State::STABLE, cfg.state);
    EXPECT_EQ(0U, cfg.newServers.servers.size());
    EXPECT_EQ("127.0.0.1:61023", cfg.localServer->address);
    EXPECT_EQ(1U, cfg.knownServers.size());

    // TODO(ongaro): test the gc code at the end of the function
}

TEST_F(ServerRaftConsensusConfigurationTest, stagingAll) {
    // TODO(ongaro): low-priority test
}

TEST_F(ServerRaftConsensusConfigurationTest, stagingMin) {
    // TODO(ongaro): low-priority test
}

TEST_F(ServerRaftConsensusConfigurationTest, getServer) {
    EXPECT_EQ(cfg.localServer, cfg.getServer(1));
    auto s = cfg.getServer(2);
    EXPECT_EQ(2U, s->serverId);
    EXPECT_EQ(s, cfg.getServer(2));
}

class ServerRaftConsensusTest : public ::testing::Test {
    ServerRaftConsensusTest()
        : globals()
        , consensus()
        , entry1()
        , entry2()
        , entry3()
        , entry4()
        , entry5()
    {
        RaftConsensus::ELECTION_TIMEOUT_MS = 5000;
        RaftConsensus::HEARTBEAT_PERIOD_MS = 2500;
        RaftConsensus::RPC_FAILURE_BACKOFF_MS = 3000;
        RaftConsensus::SOFT_RPC_SIZE_LIMIT = 1024;
        startThreads = false;
        consensus.reset(new RaftConsensus(globals));
        consensus->serverId = 1;
        Clock::useMockValue = true;
        Clock::mockValue = Clock::now();

        entry1.set_term(1);
        entry1.set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *entry1.mutable_configuration() = desc(d);

        entry2.set_term(2);
        entry2.set_type(Protocol::Raft::EntryType::DATA);
        entry2.set_data("hello");

        entry3.set_term(3);
        entry3.set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *entry3.mutable_configuration() = desc(d2);

        entry4.set_term(4);
        entry4.set_type(Protocol::Raft::EntryType::DATA);
        entry4.set_data("goodbye");

        entry5.set_term(5);
        entry5.set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *entry5.mutable_configuration() = desc(d3);
    }
    void init() {
        consensus->log.reset(new Log());
        consensus->init();
    }
    ~ServerRaftConsensusTest()
    {
        consensus->invariants.checkAll();
        EXPECT_EQ(0U, consensus->invariants.errors);
        startThreads = true;
        Clock::useMockValue = false;
    }

    Peer* getPeer(uint64_t serverId) {
        Server* server = consensus->configuration->
                            knownServers.at(serverId).get();
        return dynamic_cast<Peer*>(server);
    }

    std::shared_ptr<Peer> getPeerRef(uint64_t serverId) {
        std::shared_ptr<Server> server = consensus->configuration->
                                            knownServers.at(serverId);
        return std::dynamic_pointer_cast<Peer>(server);
    }

    Globals globals;
    std::unique_ptr<RaftConsensus> consensus;
    Log::Entry entry1;
    Log::Entry entry2;
    Log::Entry entry3;
    Log::Entry entry4;
    Log::Entry entry5;
};

class ServerRaftConsensusPTest : public ServerRaftConsensusTest {
  public:
    ServerRaftConsensusPTest()
        : peerService()
        , peerServer()
        , eventLoopThread()
    {
        peerService = std::make_shared<RPC::ServiceMock>();
        peerServer.reset(new RPC::Server(globals.eventLoop,
                                     Protocol::Common::MAX_MESSAGE_LENGTH));
        RPC::Address address("127.0.0.1:61024", 0);
        EXPECT_EQ("", peerServer->bind(address));
        peerServer->registerService(
                            Protocol::Common::ServiceId::RAFT_SERVICE,
                            peerService, 1);
        eventLoopThread = std::thread(&Event::Loop::runForever,
                                      &globals.eventLoop);
    }
    ~ServerRaftConsensusPTest()
    {
        globals.eventLoop.exit();
        eventLoopThread.join();
    }

    std::shared_ptr<RPC::ServiceMock> peerService;
    std::unique_ptr<RPC::Server> peerServer;
    std::thread eventLoopThread;
};

TEST_F(ServerRaftConsensusTest, init_blanklog)
{
    consensus->log.reset(new Log());
    consensus->init();
    EXPECT_EQ(0U, consensus->log->getLastLogIndex());
    EXPECT_EQ(0U, consensus->currentTerm);
    EXPECT_EQ(0U, consensus->votedFor);
    EXPECT_EQ(1U, consensus->configuration->localServer->serverId);
    EXPECT_EQ(Configuration::State::BLANK, consensus->configuration->state);
    EXPECT_EQ(0U, consensus->configuration->id);
    EXPECT_EQ(0U, consensus->commitIndex);
    EXPECT_LT(Clock::mockValue, consensus->startElectionAt);
    EXPECT_GT(Clock::mockValue +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS * 2),
              consensus->startElectionAt);
}

TEST_F(ServerRaftConsensusTest, init_nonblanklog)
{
    consensus->log.reset(new Log());
    Log& log = *consensus->log.get();
    log.metadata.set_current_term(30);
    log.metadata.set_voted_for(63);
    Log::Entry entry;
    entry.set_term(1);
    entry.set_type(Protocol::Raft::EntryType::CONFIGURATION);
    *entry.mutable_configuration() = desc(d);
    log.append(entry);

    Log::Entry entry2;
    entry2.set_term(2);
    entry2.set_type(Protocol::Raft::EntryType::DATA);
    entry2.set_data("hello, world");
    log.append(entry2);

    consensus->init();
    EXPECT_EQ(2U, consensus->log->getLastLogIndex());
    EXPECT_EQ(30U, consensus->currentTerm);
    EXPECT_EQ(63U, consensus->votedFor);
    EXPECT_EQ(1U, consensus->configuration->localServer->serverId);
    EXPECT_EQ("127.0.0.1:61023",
              consensus->configuration->localServer->address);
    EXPECT_EQ(Configuration::State::STABLE, consensus->configuration->state);
    EXPECT_EQ(1U, consensus->configuration->id);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
}

// TODO(ongaro): low-priority test: exit

TEST_F(ServerRaftConsensusTest, getConfiguration_notleader)
{
    init();
    Protocol::Raft::SimpleConfiguration c;
    uint64_t id;
    EXPECT_EQ(ClientResult::NOT_LEADER, consensus->getConfiguration(c, id));
}

void
setLastAckEpoch(Peer* peer)
{
    peer->lastAckEpoch = peer->consensus.currentEpoch;
}

TEST_F(ServerRaftConsensusTest, getConfiguration_retry)
{
    init();
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(2U, consensus->log->getLastLogIndex());
    EXPECT_EQ(2U, consensus->commitIndex);
    entry5.set_term(1);
    *entry5.mutable_configuration() = desc(d4);
    consensus->append(entry5);
    EXPECT_EQ(State::LEADER, consensus->state);
    EXPECT_EQ(2U, consensus->commitIndex);
    EXPECT_EQ(3U, consensus->configuration->id);
    EXPECT_EQ(Configuration::State::TRANSITIONAL,
              consensus->configuration->state);
    consensus->stateChanged.callback = std::bind(setLastAckEpoch, getPeer(2));
    Protocol::Raft::SimpleConfiguration c;
    uint64_t id;
    EXPECT_EQ(ClientResult::RETRY, consensus->getConfiguration(c, id));
}

TEST_F(ServerRaftConsensusTest, getConfiguration_ok)
{
    init();
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::LEADER, consensus->state);
    Protocol::Raft::SimpleConfiguration c;
    uint64_t id;
    EXPECT_EQ(ClientResult::SUCCESS, consensus->getConfiguration(c, id));
    EXPECT_EQ("servers { server_id: 1, address: '127.0.0.1:61023' }", c);
    EXPECT_EQ(1U, id);
}

class AppendAndCommit {
    explicit AppendAndCommit(RaftConsensus& consensus)
        : consensus(consensus)
    {
    }
    void operator()() {
        using Core::StringUtil::format;
        Log::Entry entry;
        entry.set_term(50);
        entry.set_data(format("entry%lu",
                              consensus.log->getLastLogIndex() + 1));
        consensus.commitIndex = consensus.log->append(entry);
    }
    RaftConsensus& consensus;
};

// TODO(ongaro): getLastCommittedId: low-priority test

TEST_F(ServerRaftConsensusTest, getNextEntry)
{
    init();
    consensus->append(entry1);
    consensus->append(entry2);
    consensus->append(entry3);
    consensus->append(entry4);
    consensus->stepDown(5);
    consensus->commitIndex = 4;
    consensus->stateChanged.callback = std::bind(&Consensus::exit,
                                                 consensus.get());
    Consensus::Entry e1 = consensus->getNextEntry(0);
    EXPECT_EQ(1U, e1.entryId);
    EXPECT_FALSE(e1.hasData);
    Consensus::Entry e2 = consensus->getNextEntry(e1.entryId);
    EXPECT_EQ(2U, e2.entryId);
    EXPECT_TRUE(e2.hasData);
    EXPECT_EQ("hello", e2.data);
    Consensus::Entry e3 = consensus->getNextEntry(e2.entryId);
    EXPECT_EQ(3U, e3.entryId);
    EXPECT_FALSE(e3.hasData);
    Consensus::Entry e4 = consensus->getNextEntry(e3.entryId);
    EXPECT_EQ(4U, e4.entryId);
    EXPECT_TRUE(e4.hasData);
    EXPECT_EQ("goodbye", e4.data);
    EXPECT_THROW(consensus->getNextEntry(e4.entryId),
                 ThreadInterruptedException);
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_callerStale)
{
    init();
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(8);
    request.set_prev_log_index(0);
    request.set_commit_index(0);
    consensus->stepDown(11);
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 11 "
              "success: false ",
              response);
}

// this tests the leaderId == 0 branch, setElectionTimer(), and heartbeat
TEST_F(ServerRaftConsensusTest, handleAppendEntries_newLeaderAndCommittedId)
{
    init();
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(5);
    request.set_prev_log_index(1);
    request.set_commit_index(1);
    consensus->stepDown(8);
    consensus->append(entry5);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    EXPECT_EQ(9U, consensus->currentTerm);
    EXPECT_EQ(0U, consensus->commitIndex);
    Clock::mockValue += milliseconds(10000);
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ(3U, consensus->leaderId);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    EXPECT_EQ(0U, consensus->votedFor);
    EXPECT_EQ(10U, consensus->currentTerm);
    EXPECT_LT(Clock::mockValue, consensus->startElectionAt);
    EXPECT_GT(Clock::mockValue +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS * 2),
              consensus->startElectionAt);
    EXPECT_EQ(1U, consensus->commitIndex);
    EXPECT_EQ("term: 10 "
              "success: true ",
              response);
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_rejectGap)
{
    init();
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(1);
    request.set_prev_log_index(1);
    request.set_commit_index(1);
    consensus->stepDown(10);
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 10 "
              "success: false ",
              response);
    EXPECT_EQ(0U, consensus->commitIndex);
    EXPECT_EQ(0U, consensus->log->getLastLogIndex());
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_rejectPrevLogTerm)
{
    init();
    consensus->append(entry1);
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(10);
    request.set_prev_log_index(1);
    request.set_commit_index(1);
    consensus->stepDown(10);
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 10 "
              "success: false ",
              response);
    EXPECT_EQ(0U, consensus->commitIndex);
    EXPECT_EQ(1U, consensus->log->getLastLogIndex());
    EXPECT_EQ(1U, consensus->log->getTerm(1));
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_append)
{
    init();
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(0);
    request.set_prev_log_index(0);
    request.set_commit_index(1);
    Protocol::Raft::Entry* e1 = request.add_entries();
    e1->set_term(4);
    e1->set_type(Protocol::Raft::EntryType::CONFIGURATION);
    *e1->mutable_configuration() = desc(d3);
    Protocol::Raft::Entry* e2 = request.add_entries();
    e2->set_term(5);
    e2->set_type(Protocol::Raft::EntryType::DATA);
    e2->set_data("hello");
    consensus->stepDown(10);
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 10 "
              "success: true ",
              response);
    EXPECT_EQ(1U, consensus->commitIndex);
    EXPECT_EQ(2U, consensus->log->getLastLogIndex());
    EXPECT_EQ(1U, consensus->configuration->id);
    const Log::Entry& l1 = consensus->log->getEntry(1);
    EXPECT_EQ(4U, l1.term());
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l1.type());
    EXPECT_EQ(d3, l1.configuration());
    const Log::Entry& l2 = consensus->log->getEntry(2);
    EXPECT_EQ(5U, l2.term());
    EXPECT_EQ(Protocol::Raft::EntryType::DATA, l2.type());
    EXPECT_EQ("hello", l2.data());
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_truncate)
{
    // Log:
    // 1,t1: config { s1 }
    // 2,t2: no op
    // 3,t2: "hello"
    // 4,t2: config { s1, s2 }
    // later replaced with
    // 4,t3: "bar"
    init();
    consensus->stepDown(1);
    consensus->append(entry1);
    consensus->startNewElection();
    consensus->append(entry2);
    consensus->advanceCommittedId();
    entry5.set_term(2);
    consensus->append(entry5);
    consensus->advanceCommittedId(); // shouldn't do anything
    EXPECT_EQ(3U, consensus->commitIndex);

    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(2);
    request.set_term(3);
    request.set_prev_log_term(2);
    request.set_prev_log_index(2);
    request.set_commit_index(0);
    Protocol::Raft::Entry* e1 = request.add_entries();
    e1->set_term(2);
    e1->set_type(Protocol::Raft::EntryType::DATA);
    e1->set_data("hello");
    Protocol::Raft::Entry* e2 = request.add_entries();
    e2->set_term(3);
    e2->set_type(Protocol::Raft::EntryType::DATA);
    e2->set_data("bar");

    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 3 "
              "success: true ",
              response);
    EXPECT_EQ(3U, consensus->commitIndex);
    EXPECT_EQ(4U, consensus->log->getLastLogIndex());
    EXPECT_EQ(1U, consensus->configuration->id);
    const Log::Entry& l1 = consensus->log->getEntry(1);
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l1.type());
    EXPECT_EQ(d, l1.configuration());
    const Log::Entry& l2 = consensus->log->getEntry(2);
    EXPECT_EQ(Protocol::Raft::EntryType::NOOP, l2.type());
    const Log::Entry& l3 = consensus->log->getEntry(3);
    EXPECT_EQ("hello", l3.data());
    const Log::Entry& l4 = consensus->log->getEntry(4);
    EXPECT_EQ("bar", l4.data());
}

TEST_F(ServerRaftConsensusTest, handleAppendEntries_duplicate)
{
    init();
    consensus->stepDown(10);
    consensus->append(entry1);
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_prev_log_term(0);
    request.set_prev_log_index(0);
    request.set_commit_index(0);
    Protocol::Raft::Entry* e1 = request.add_entries();
    e1->set_term(1);
    e1->set_type(Protocol::Raft::EntryType::DATA);
    e1->set_data("hello");
    consensus->handleAppendEntries(request, response);
    EXPECT_EQ("term: 10 "
              "success: true ",
              response);
    EXPECT_EQ(1U, consensus->log->getLastLogIndex());
    const Log::Entry& l1 = consensus->log->getEntry(1);
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l1.type());
    EXPECT_EQ(d, l1.configuration());
    EXPECT_EQ("", l1.data());
}

TEST_F(ServerRaftConsensusTest, handleRequestVote)
{
    init();
    Protocol::Raft::RequestVote::Request request;
    Protocol::Raft::RequestVote::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_last_log_term(1);
    request.set_last_log_index(2);

    // as leader, log is ok
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::LEADER, consensus->state);
    consensus->handleRequestVote(request, response);
    EXPECT_EQ("term: 10 "
              "granted: true ",
              response);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    EXPECT_EQ(3U, consensus->votedFor);

    // as candidate, log is not ok
    consensus->append(entry5);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    request.set_term(12);
    TimePoint oldStartElectionAt = consensus->startElectionAt;
    Clock::mockValue += milliseconds(2);
    consensus->handleRequestVote(request, response);
    EXPECT_EQ("term: 12 "
              "granted: false ",
              response);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    // check that the election timer was not reset
    EXPECT_EQ(oldStartElectionAt, consensus->startElectionAt);
    EXPECT_EQ(0U, consensus->votedFor);

    // as candidate, log is ok
    request.set_last_log_term(9);
    consensus->handleRequestVote(request, response);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    EXPECT_EQ("term: 12 "
              "granted: true ",
              response);
    EXPECT_EQ(3U, consensus->votedFor);
}

TEST_F(ServerRaftConsensusTest, handleRequestVote_termStale)
{
    init();
    Protocol::Raft::RequestVote::Request request;
    Protocol::Raft::RequestVote::Response response;
    request.set_server_id(3);
    request.set_term(10);
    request.set_last_log_term(1);
    request.set_last_log_index(1);
    consensus->stepDown(11);
    consensus->handleRequestVote(request, response);
    EXPECT_EQ("term: 11 "
              "granted: false ",
              response);
    Clock::mockValue += milliseconds(100000);
    // don't hand out vote, don't reset follower timer
    EXPECT_EQ(0U, consensus->votedFor);
    EXPECT_GT(Clock::mockValue, consensus->startElectionAt);
}

// TODO(ongardie): low-priority test: replicate

TEST_F(ServerRaftConsensusTest, setConfiguration_notLeader)
{
    init();
    Protocol::Raft::SimpleConfiguration c;
    EXPECT_EQ(ClientResult::NOT_LEADER, consensus->setConfiguration(1, c));
}

TEST_F(ServerRaftConsensusTest, setConfiguration_changed)
{
    init();
    consensus->append(entry1);
    consensus->startNewElection();
    Protocol::Raft::SimpleConfiguration c;
    EXPECT_EQ(ClientResult::FAIL, consensus->setConfiguration(0, c));
    consensus->configuration->setStagingServers(sdesc(""));
    consensus->stateChanged.notify_all();
    EXPECT_EQ(Configuration::State::STAGING, consensus->configuration->state);
    EXPECT_EQ(ClientResult::FAIL, consensus->setConfiguration(1, c));
}

void
setConfigurationHelper(RaftConsensus* consensus)
{
    TimePoint waitUntil(
                consensus->stateChanged.lastWaitUntilTimeSinceEpoch);
    EXPECT_EQ(round(Clock::mockValue) +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS),
              waitUntil);
    Clock::mockValue += milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS);
}

TEST_F(ServerRaftConsensusTest, setConfiguration_catchupFail)
{
    init();
    consensus->append(entry1);
    consensus->startNewElection();
    Protocol::Raft::SimpleConfiguration c = sdesc(
        "servers { server_id: 2, address: '127.0.0.1:61024' }");
    consensus->stateChanged.callback = std::bind(setConfigurationHelper,
                                                 consensus.get());
    EXPECT_EQ(ClientResult::FAIL, consensus->setConfiguration(1, c));
}

void
setConfigurationHelper2(RaftConsensus* consensus)
{
    Server* server = consensus->configuration->knownServers.at(2).get();
    Peer* peer = dynamic_cast<Peer*>(server);
    peer->isCaughtUp_ = true;
    consensus->stateChanged.callback = std::bind(&RaftConsensus::stepDown,
                                                 consensus, 10);
}

TEST_F(ServerRaftConsensusTest, setConfiguration_replicateFail)
{
    init();
    consensus->append(entry1);
    consensus->stepDown(1);
    consensus->startNewElection();
    Protocol::Raft::SimpleConfiguration c = sdesc(
        "servers { server_id: 2, address: '127.0.0.1:61024' }");
    consensus->stateChanged.callback = std::bind(setConfigurationHelper2,
                                                 consensus.get());
    EXPECT_EQ(ClientResult::NOT_LEADER, consensus->setConfiguration(1, c));
    // 1: entry1, 2: no-op, 3: transitional
    EXPECT_EQ(3U, consensus->log->getLastLogIndex());
    const Log::Entry& l2 = consensus->log->getEntry(3);
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l2.type());
    EXPECT_EQ("prev_configuration {"
                  "servers { server_id: 1, address: '127.0.0.1:61023' }"
              "}"
              "next_configuration {"
                  "servers { server_id: 2, address: '127.0.0.1:61024' }"
              "}",
              l2.configuration());
}

TEST_F(ServerRaftConsensusTest, setConfiguration_replicateOkJustUs)
{
    init();
    consensus->append(entry1);
    consensus->stepDown(1);
    consensus->startNewElection();
    Protocol::Raft::SimpleConfiguration c = sdesc(
        "servers { server_id: 1, address: '127.0.0.1:61024' }");
    EXPECT_EQ(ClientResult::SUCCESS, consensus->setConfiguration(1, c));
    // 1: entry1, 2: no-op, 3: transitional, 4: new config
    EXPECT_EQ(4U, consensus->log->getLastLogIndex());
    const Log::Entry& l3 = consensus->log->getEntry(4);
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l3.type());
    EXPECT_EQ("prev_configuration {"
                  "servers { server_id: 1, address: '127.0.0.1:61024' }"
              "}",
              l3.configuration());
}

// used in setConfiguration_replicateOkNontrivial
class SetConfigurationHelper3 {
    explicit SetConfigurationHelper3(RaftConsensus* consensus)
        : consensus(consensus)
        , iter(1)
    {
    }
    void operator()() {
        Server* server = consensus->configuration->knownServers.at(2).get();
        Peer* peer = dynamic_cast<Peer*>(server);
        if (iter == 1) {
            peer->isCaughtUp_ = true;
        } else if (iter == 2) { // no-op entry
            peer->lastAgreeIndex = 2;
            consensus->advanceCommittedId();
        } else if (iter == 3) {
            peer->lastAgreeIndex = 3;
            consensus->advanceCommittedId();
        } else if (iter == 4) {
            peer->lastAgreeIndex = 4;
            consensus->advanceCommittedId();
        } else {
            FAIL();
        }
        ++iter;
    }
    RaftConsensus* consensus;
    uint64_t iter;
};

TEST_F(ServerRaftConsensusTest, setConfiguration_replicateOkNontrivial)
{
    init();
    consensus->append(entry1);
    consensus->stepDown(1);
    consensus->startNewElection();
    Protocol::Raft::SimpleConfiguration c = sdesc(
        "servers { server_id: 2, address: '127.0.0.1:61024' }");
    consensus->stateChanged.callback =
        SetConfigurationHelper3(consensus.get());
    EXPECT_EQ(ClientResult::SUCCESS, consensus->setConfiguration(1, c));
    EXPECT_EQ(4U, consensus->log->getLastLogIndex());
}

class CandidacyThreadMainHelper {
    explicit CandidacyThreadMainHelper(RaftConsensus& consensus)
        : consensus(consensus)
        , iter(1)
    {
    }
    void operator()() {
        if (iter == 1) {
            EXPECT_EQ(State::FOLLOWER, consensus.state);
            Clock::mockValue = consensus.startElectionAt + milliseconds(1);
        } else {
            EXPECT_EQ(State::CANDIDATE, consensus.state);
            consensus.exit();
        }
        ++iter;
    }
    RaftConsensus& consensus;
    int iter;
};

// The first time through the while loop, we don't want to start a new election
// and want to wait on the condition variable. The second time through, we want
// to start a new election. Then we want to exit.
TEST_F(ServerRaftConsensusTest, timerThreadMain)
{
    init();
    Clock::mockValue = consensus->startElectionAt - milliseconds(1);
    Clock::useMockValue = true;
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->append(entry5);
    consensus->stateChanged.callback = CandidacyThreadMainHelper(*consensus);
    consensus->timerThreadMain();
}

// used in peerThreadMain test
class FollowerThreadMainHelper {
    explicit FollowerThreadMainHelper(RaftConsensus& consensus, Peer& peer)
        : consensus(consensus)
        , peer(peer)
        , iter(1)
    {
    }
    void operator()() {
        TimePoint waitUntil(
                    consensus.stateChanged.lastWaitUntilTimeSinceEpoch);

        if (iter == 1) {
            // expect to block forever as a follower
            EXPECT_EQ(round(TimePoint::max()), waitUntil);
            // set the peer's backoff to prepare for next iteration
            peer.backoffUntil = Clock::mockValue + milliseconds(1);
        } else if (iter == 2) {
            // still a follower so nothing to do, but this time we have to
            // block until backoff is over
            EXPECT_EQ(round(Clock::mockValue + milliseconds(1)), waitUntil);
            Clock::mockValue += milliseconds(2);
            // move to candidacy
            consensus.startNewElection();
        } else if (iter == 3) {
            // we should have just requested peer's vote, so expect to return
            // immediately
            EXPECT_EQ(round(TimePoint::min()), waitUntil);
        } else if (iter == 4) {
            // the vote was granted, so there's nothing left to do for this
            // peer as a candidate, sleep forever
            EXPECT_EQ(round(TimePoint::max()), waitUntil);
            // move to leader state
            consensus.becomeLeader();
        } else if (iter == 5) {
            // we should have just sent a heartbeat, so expect to return
            // immediately
            EXPECT_EQ(round(TimePoint::min()), waitUntil);
        } else if (iter == 6) {
            // expect to block until the next heartbeat
            EXPECT_EQ(round(peer.nextHeartbeatTime), waitUntil);
            Clock::mockValue = peer.nextHeartbeatTime + milliseconds(1);
        } else if (iter == 7) {
            // we should have just sent a heartbeat, so expect to return
            // immediately
            EXPECT_EQ(round(TimePoint::min()), waitUntil);
        } else if (iter == 8) {
            // expect to block until the next heartbeat
            EXPECT_EQ(round(peer.nextHeartbeatTime), waitUntil);
            // exit
            consensus.exit();
            EXPECT_TRUE(peer.exiting);
        } else {
            FAIL() << iter;
        }
        ++iter;
    }
    RaftConsensus& consensus;
    Peer& peer;
    int iter;
};

TEST_F(ServerRaftConsensusPTest, peerThreadMain)
{
    // Log:
    // 1,t5: cfg { server 1,2,3,4,5 }
    // 2,t6: no-op
    init();
    consensus->stepDown(5);
    *entry5.mutable_configuration() = desc(
        "prev_configuration {"
        "    servers { server_id: 1, address: '127.0.0.1:61023' }"
        "    servers { server_id: 2, address: '127.0.0.1:61024' }"
        "    servers { server_id: 3, address: '127.0.0.1:61024' }"
        "    servers { server_id: 4, address: '127.0.0.1:61024' }"
        "    servers { server_id: 5, address: '127.0.0.1:61024' }"
        "}");
    consensus->append(entry5);
    std::shared_ptr<Peer> peer = getPeerRef(2);
    consensus->stateChanged.callback = FollowerThreadMainHelper(*consensus,
                                                                *peer);
    ++consensus->numPeerThreads;

    // first requestVote RPC succeeds
    Protocol::Raft::RequestVote::Request vrequest;
    vrequest.set_server_id(1);
    vrequest.set_recipient_id(2);
    vrequest.set_term(6);
    vrequest.set_last_log_term(5);
    vrequest.set_last_log_index(1);
    Protocol::Raft::RequestVote::Response vresponse;
    vresponse.set_term(5);
    vresponse.set_granted(true);
    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       vrequest, vresponse);

    // first appendEntries sends heartbeat (accept it)
    Protocol::Raft::AppendEntries::Request arequest;
    arequest.set_server_id(1);
    arequest.set_recipient_id(2);
    arequest.set_term(6);
    arequest.set_prev_log_term(6);
    arequest.set_prev_log_index(2);
    arequest.set_commit_index(0);
    Protocol::Raft::AppendEntries::Response aresponse;
    aresponse.set_term(6);
    aresponse.set_success(true);
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       arequest, aresponse);

    // second appendEntries sends heartbeat
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       arequest, aresponse);

    consensus->peerThreadMain(peer);
}

class StepDownThreadMainHelper {
    explicit StepDownThreadMainHelper(RaftConsensus& consensus)
        : consensus(consensus)
        , iter(1)
    {
    }
    void operator()() {
        if (iter == 1) {
            consensus.startNewElection();
        } else {
            consensus.exit();
        }
        ++iter;
    }
    RaftConsensus& consensus;
    int iter;
};

TEST_F(ServerRaftConsensusTest, stepDownThreadMain_oneServerNoInfiniteLoop)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->stateChanged.callback = StepDownThreadMainHelper(*consensus);
    consensus->stepDownThreadMain();
    EXPECT_EQ(State::LEADER, consensus->state);
}

class StepDownThreadMainHelper2 {
    explicit StepDownThreadMainHelper2(RaftConsensus& consensus,
                                       Peer& peer)
        : consensus(consensus)
        , peer(peer)
        , iter(1)
    {
    }
    void operator()() {
        if (iter == 1) {
            EXPECT_EQ(1U, consensus.currentEpoch);
            consensus.stepDown(consensus.currentTerm + 1);
            consensus.startNewElection();
            consensus.becomeLeader();
        } else if (iter == 2) {
            EXPECT_EQ(2U, consensus.currentEpoch);
            peer.lastAckEpoch = 2;
        } else if (iter == 3) {
            EXPECT_EQ(3U, consensus.currentEpoch);
            Clock::mockValue +=
                milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS);
        } else if (iter == 4) {
            EXPECT_EQ(3U, consensus.currentEpoch);
            consensus.exit();
        } else {
            FAIL();
        }
        ++iter;
    }
    RaftConsensus& consensus;
    Peer& peer;
    int iter;
};


TEST_F(ServerRaftConsensusTest, stepDownThreadMain_twoServers)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry5);
    consensus->startNewElection();
    consensus->becomeLeader();
    consensus->currentEpoch = 0;
    consensus->stateChanged.callback = StepDownThreadMainHelper2(*consensus,
                                                                 *getPeer(2));
    consensus->stepDownThreadMain();
}

TEST_F(ServerRaftConsensusTest, advanceCommittedId_noAdvanceMissingQuorum)
{
    init();
    consensus->append(entry1);
    consensus->append(entry5);
    consensus->stepDown(5);
    consensus->startNewElection();
    consensus->becomeLeader();
    consensus->advanceCommittedId();
    EXPECT_EQ(State::LEADER, consensus->state);
    EXPECT_EQ(0U, consensus->commitIndex);
}

TEST_F(ServerRaftConsensusTest,
       advanceCommittedId_noAdvanceNoEntryFromCurrentTerm)
{
    init();
    consensus->append(entry1);
    consensus->append(entry5);
    consensus->stepDown(5);
    consensus->startNewElection();
    consensus->becomeLeader();
    getPeer(2)->lastAgreeIndex = 2;
    consensus->advanceCommittedId();
    EXPECT_EQ(State::LEADER, consensus->state);
    EXPECT_EQ(0U, consensus->commitIndex);
    getPeer(2)->lastAgreeIndex = 3;
    consensus->advanceCommittedId();
    EXPECT_EQ(3U, consensus->commitIndex);
}

TEST_F(ServerRaftConsensusTest, advanceCommittedId_commitCfgWithoutSelf)
{
    // Log:
    // 1,t1: cfg { server 1 }
    // 2,t6: no op
    // 3,t6: transitional cfg { server 1 } -> { server 2 }
    // 4,t6: cfg { server 2 }
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection();
    entry1.set_term(6);
    *entry1.mutable_configuration() = desc(
        "prev_configuration {"
        "    servers { server_id: 1, address: '127.0.0.1:61023' }"
        "}"
        "next_configuration {"
            "servers { server_id: 2, address: '127.0.0.1:61024' }"
        "}");
    consensus->append(entry1);
    getPeer(2)->lastAgreeIndex = 3;
    consensus->advanceCommittedId();
    EXPECT_EQ(3U, consensus->commitIndex);
    EXPECT_EQ(4U, consensus->log->getLastLogIndex());
    EXPECT_EQ(State::LEADER, consensus->state);

    getPeer(2)->lastAgreeIndex = 4;
    consensus->advanceCommittedId();
    EXPECT_EQ(4U, consensus->commitIndex);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
}

TEST_F(ServerRaftConsensusTest, advanceCommittedId_commitTransitionToSelf)
{
    // Log:
    // 1,t1: cfg { server 1:61023 }
    // 2,t6: no op
    // 3,t6: transitional cfg { server 1:61023 } -> { server 1:61025 }
    // 4,t6: cfg { server 1:61025 }
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::LEADER, consensus->state);
    entry3.set_term(6);
    consensus->append(entry3);
    consensus->advanceCommittedId();
    EXPECT_EQ(4U, consensus->commitIndex);
    EXPECT_EQ(4U, consensus->log->getLastLogIndex());
    const Log::Entry& l3 = consensus->log->getEntry(4);
    EXPECT_EQ(Protocol::Raft::EntryType::CONFIGURATION, l3.type());
    EXPECT_EQ("prev_configuration {"
                  "servers { server_id: 1, address: '127.0.0.1:61025' }"
              "}",
              l3.configuration());
}

TEST_F(ServerRaftConsensusTest, append)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->append(entry2);
    EXPECT_EQ(1U, consensus->configuration->id);
    EXPECT_EQ(2U, consensus->log->getLastLogIndex());
}

// used in AppendEntries tests
class ServerRaftConsensusPATest : public ServerRaftConsensusPTest {
    ServerRaftConsensusPATest()
        : peer()
        , request()
        , response()
    {
        // Log:
        // 1,t1: cfg { server 1 }
        // 2,t2: "hello"
        // 3,t6: no-op
        // 4,t6: cfg { server 1,2 }
        init();
        consensus->append(entry1);
        consensus->append(entry2);
        consensus->stepDown(5);
        consensus->startNewElection();
        entry5.set_term(6);
        consensus->append(entry5);
        EXPECT_EQ(State::LEADER, consensus->state);
        peer = getPeerRef(2);

        // For some reason or other, these tests are written to assume the
        // leader has determined that peer and it diverge on the first log
        // entry.
        EXPECT_EQ(5U, peer->nextIndex);
        EXPECT_TRUE(peer->forceHeartbeat);
        peer->nextIndex = 1;
        peer->forceHeartbeat = false;

        request.set_server_id(1);
        request.set_recipient_id(2);
        request.set_term(6);
        request.set_prev_log_term(0);
        request.set_prev_log_index(0);
        request.set_commit_index(3);
        Protocol::Raft::Entry* e1 = request.add_entries();
        e1->set_term(1);
        e1->set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *e1->mutable_configuration() = entry1.configuration();
        Protocol::Raft::Entry* e2 = request.add_entries();
        e2->set_term(2);
        e2->set_type(Protocol::Raft::EntryType::DATA);
        e2->set_data(entry2.data());
        Protocol::Raft::Entry* enop = request.add_entries();
        enop->set_term(6);
        enop->set_type(Protocol::Raft::EntryType::NOOP);
        Protocol::Raft::Entry* e3 = request.add_entries();
        e3->set_term(6);
        e3->set_type(Protocol::Raft::EntryType::CONFIGURATION);
        *e3->mutable_configuration() = entry5.configuration();

        response.set_term(6);
        response.set_success(true);
    }

    std::shared_ptr<Peer> peer;
    Protocol::Raft::AppendEntries::Request request;
    Protocol::Raft::AppendEntries::Response response;
};

TEST_F(ServerRaftConsensusPATest, appendEntries_rpcFailed)
{
    peerService->closeSession(Protocol::Raft::OpCode::APPEND_ENTRIES, request);
    // expect warning
    LogCabin::Core::Debug::setLogPolicy({
        {"Server/RaftConsensus.cc", "ERROR"}
    });
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->appendEntries(lockGuard, *peer);
    EXPECT_LT(Clock::now(), peer->backoffUntil);
    EXPECT_EQ(0U, peer->lastAgreeIndex);
}

TEST_F(ServerRaftConsensusPATest, appendEntries_limitSizeAndIgnoreResult)
{
    RaftConsensus::SOFT_RPC_SIZE_LIMIT = 1;
    request.mutable_entries()->RemoveLast();
    request.mutable_entries()->RemoveLast();
    request.mutable_entries()->RemoveLast();
    request.set_commit_index(1);
    peer->exiting = true;
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       request, response);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->appendEntries(lockGuard, *peer);
    EXPECT_EQ(0U, peer->lastAgreeIndex);
}

TEST_F(ServerRaftConsensusPATest, appendEntries_forceHeartbeat)
{
    peer->forceHeartbeat = true;
    request.mutable_entries()->Clear();
    request.set_commit_index(0);
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       request, response);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->appendEntries(lockGuard, *peer);
    EXPECT_FALSE(peer->forceHeartbeat);
}

TEST_F(ServerRaftConsensusPATest, appendEntries_termStale)
{
    response.set_term(10);
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       request, response);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->appendEntries(lockGuard, *peer);
    EXPECT_EQ(0U, peer->lastAgreeIndex);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    EXPECT_EQ(10U, consensus->currentTerm);
}

TEST_F(ServerRaftConsensusPATest, appendEntries_ok)
{
    peerService->reply(Protocol::Raft::OpCode::APPEND_ENTRIES,
                       request, response);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->appendEntries(lockGuard, *peer);
    EXPECT_EQ(consensus->currentEpoch, peer->lastAckEpoch);
    EXPECT_EQ(4U, peer->lastAgreeIndex);
    EXPECT_EQ(Clock::mockValue +
              milliseconds(RaftConsensus::HEARTBEAT_PERIOD_MS),
              peer->nextHeartbeatTime);

    // TODO(ongaro): test catchup code
}

TEST_F(ServerRaftConsensusTest, becomeLeader)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    EXPECT_EQ(5U, consensus->currentTerm);
    consensus->startNewElection(); // calls becomeLeader
    EXPECT_EQ(State::LEADER, consensus->state);
    EXPECT_EQ(6U, consensus->currentTerm);
    EXPECT_EQ(1U, consensus->leaderId);
    EXPECT_EQ(2U, consensus->log->getLastLogIndex());
    EXPECT_EQ(2U, consensus->commitIndex);
    const Log::Entry& nop = consensus->log->getEntry(2);
    EXPECT_EQ(6U, nop.term());
    EXPECT_EQ(Protocol::Raft::EntryType::NOOP, nop.type());
    EXPECT_EQ(TimePoint::max(), consensus->startElectionAt);
}

TEST_F(ServerRaftConsensusTest, interruptAll)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->append(entry5);
    consensus->stateChanged.notificationCount = 0;
    consensus->interruptAll();
    Peer& peer = *getPeer(2);
    EXPECT_EQ("RPC canceled by user", peer.rpc.getErrorMessage());
    EXPECT_EQ(1U, consensus->stateChanged.notificationCount);
}

TEST_F(ServerRaftConsensusTest, replicateEntry_notLeader)
{
    init();
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    EXPECT_EQ(ClientResult::NOT_LEADER,
              consensus->replicateEntry(entry2, lockGuard).first);
}

TEST_F(ServerRaftConsensusTest, replicateEntry_okJustUs)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection();
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    std::pair<ClientResult, uint64_t> result =
        consensus->replicateEntry(entry2, lockGuard);
    EXPECT_EQ(ClientResult::SUCCESS, result.first);
    // 1: entry1, 2: no-op, 3: entry2
    EXPECT_EQ(3U, result.second);
}

TEST_F(ServerRaftConsensusTest, replicateEntry_termChanged)
{
    init();
    consensus->stepDown(4);
    consensus->append(entry1);
    consensus->startNewElection();
    consensus->append(entry5);
    EXPECT_EQ(State::LEADER, consensus->state);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->stateChanged.callback = std::bind(&RaftConsensus::stepDown,
                                                 consensus.get(), 7);
    EXPECT_EQ(ClientResult::NOT_LEADER,
              consensus->replicateEntry(entry2, lockGuard).first);
}

TEST_F(ServerRaftConsensusPTest, requestVote_rpcFailed)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry5);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    Peer& peer = *getPeer(2);

    Protocol::Raft::RequestVote::Request request;
    request.set_server_id(1);
    request.set_recipient_id(2);
    request.set_term(6);
    request.set_last_log_term(5);
    request.set_last_log_index(1);

    peerService->closeSession(Protocol::Raft::OpCode::REQUEST_VOTE, request);
    // expect warning
    LogCabin::Core::Debug::setLogPolicy({
        {"Server/RaftConsensus.cc", "ERROR"}
    });
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->requestVote(lockGuard, peer);
    EXPECT_LT(Clock::now(), peer.backoffUntil);
    EXPECT_FALSE(peer.requestVoteDone);
}

TEST_F(ServerRaftConsensusPTest, requestVote_ignoreResult)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry5);
    // don't become candidate so the response is ignored
    Peer& peer = *getPeer(2);

    Protocol::Raft::RequestVote::Request request;
    request.set_server_id(1);
    request.set_recipient_id(2);
    request.set_term(5);
    request.set_last_log_term(5);
    request.set_last_log_index(1);

    Protocol::Raft::RequestVote::Response response;
    response.set_term(5);
    response.set_granted(true);

    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       request, response);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->requestVote(lockGuard, peer);
    EXPECT_FALSE(peer.requestVoteDone);
}

TEST_F(ServerRaftConsensusPTest, requestVote_termStale)
{
    // Log:
    // 1,t1: cfg { server 1 }
    // 2,t6: no op
    // 3,t6: transitional cfg { server 1 } -> { server 2 }
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection(); // become leader
    *entry1.mutable_configuration() = desc(d4);
    entry1.set_term(6);
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    Peer& peer = *getPeer(2);

    Protocol::Raft::RequestVote::Request request;
    request.set_server_id(1);
    request.set_recipient_id(2);
    request.set_term(7);
    request.set_last_log_term(6);
    request.set_last_log_index(3);

    Protocol::Raft::RequestVote::Response response;
    response.set_term(8);
    response.set_granted(false);

    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       request, response);
    TimePoint oldStartElectionAt = consensus->startElectionAt;
    Clock::mockValue += milliseconds(2);
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    consensus->requestVote(lockGuard, peer);
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    // check that the election timer was not reset
    EXPECT_EQ(oldStartElectionAt, consensus->startElectionAt);
    EXPECT_EQ(8U, consensus->currentTerm);
}

TEST_F(ServerRaftConsensusPTest, requestVote_termOkAsLeader)
{
    // Log:
    // 1,t1: cfg { server 1,2,3,4,5 }
    // 2,t2: "hello"
    // 3,t2: "hello"
    // 4,t2: "hello"
    init();
    consensus->stepDown(5);
    *entry1.mutable_configuration() = desc(
        "prev_configuration {"
        "    servers { server_id: 1, address: '127.0.0.1:61023' }"
        "    servers { server_id: 2, address: '127.0.0.1:61024' }"
        "    servers { server_id: 3, address: '127.0.0.1:61024' }"
        "    servers { server_id: 4, address: '127.0.0.1:61024' }"
        "    servers { server_id: 5, address: '127.0.0.1:61024' }"
        "}");
    consensus->append(entry1);
    consensus->append(entry2);
    consensus->append(entry2);
    consensus->append(entry2);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    consensus->currentEpoch = 1000;
    Peer& peer2 = *getPeer(2);
    Peer& peer3 = *getPeer(3);
    Peer& peer4 = *getPeer(4);

    std::unique_lock<Mutex> lockGuard(consensus->mutex);

    // 1. Get response from peer2 but don't get its vote.
    Protocol::Raft::RequestVote::Request request;
    request.set_server_id(1);
    request.set_recipient_id(2);
    request.set_term(6);
    request.set_last_log_term(2);
    request.set_last_log_index(4);

    Protocol::Raft::RequestVote::Response response;
    response.set_term(6);
    response.set_granted(false);

    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       request, response);
    consensus->requestVote(lockGuard, peer2);
    EXPECT_TRUE(peer2.requestVoteDone);
    EXPECT_EQ(1000U, peer2.lastAckEpoch);
    EXPECT_EQ(State::CANDIDATE, consensus->state);

    // 2. Get vote from peer3, still a candidate
    request.set_recipient_id(3);
    response.set_granted(true);
    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       request, response);
    consensus->requestVote(lockGuard, peer3);
    EXPECT_TRUE(peer3.requestVoteDone);
    EXPECT_EQ(1000U, peer3.lastAckEpoch);
    EXPECT_EQ(State::CANDIDATE, consensus->state);

    // 3. Get vote from peer4, become leader
    request.set_recipient_id(4);
    peerService->reply(Protocol::Raft::OpCode::REQUEST_VOTE,
                       request, response);
    consensus->requestVote(lockGuard, peer4);
    EXPECT_TRUE(peer4.requestVoteDone);
    EXPECT_EQ(1000U, peer4.lastAckEpoch);
    EXPECT_EQ(State::LEADER, consensus->state);
}

TEST_F(ServerRaftConsensusTest, scanForConfiguration)
{
    init();
    consensus->stepDown(5);
    consensus->scanForConfiguration();
    EXPECT_EQ(0U, consensus->configuration->id);
    consensus->append(entry1);
    consensus->scanForConfiguration();
    EXPECT_EQ(1U, consensus->configuration->id);
    consensus->append(entry3);
    consensus->scanForConfiguration();
    EXPECT_EQ(2U, consensus->configuration->id);
}

TEST_F(ServerRaftConsensusTest, setElectionTimer)
{
    // TODO(ongaro): seed the random number generator and make sure the values
    // look sane
    init();
    for (uint64_t i = 0; i < 100; ++i) {
        consensus->setElectionTimer();
        EXPECT_LE(Clock::now() +
                  milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS),
                  consensus->startElectionAt);
        EXPECT_GE(Clock::now() +
                  milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS) * 2,
                  consensus->startElectionAt);
    }
}

TEST_F(ServerRaftConsensusTest, startNewElection)
{
    init();

    // no configuration yet -> no op
    consensus->startNewElection();
    EXPECT_EQ(State::FOLLOWER, consensus->state);
    EXPECT_EQ(0U, consensus->currentTerm);
    EXPECT_LT(Clock::now(), consensus->startElectionAt);
    EXPECT_GT(Clock::now() +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS) * 2,
              consensus->startElectionAt);

    // need other votes to win
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->append(entry5);
    consensus->startNewElection();
    EXPECT_EQ(State::CANDIDATE, consensus->state);
    EXPECT_EQ(6U, consensus->currentTerm);
    EXPECT_EQ(0U, consensus->leaderId);
    EXPECT_EQ(1U, consensus->votedFor);
    EXPECT_LT(Clock::now(), consensus->startElectionAt);
    EXPECT_GT(Clock::now() +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS) * 2,
              consensus->startElectionAt);

    // already won
    consensus->stepDown(7);
    entry1.set_term(7);
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::LEADER, consensus->state);

    // not part of current configuration
    consensus->stepDown(10);
    entry1.set_term(9);
    *entry1.mutable_configuration() = desc(
        "prev_configuration {"
            "servers { server_id: 2, address: '127.0.0.1:61025' }"
        "}");
    consensus->append(entry1);
    consensus->startNewElection();
    EXPECT_EQ(State::FOLLOWER, consensus->state);
}

TEST_F(ServerRaftConsensusTest, stepDown)
{
    init();

    // set up
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection();
    consensus->configuration->setStagingServers(sdesc(""));
    consensus->stateChanged.notify_all();
    EXPECT_NE(0U, consensus->leaderId);
    EXPECT_NE(0U, consensus->votedFor);
    EXPECT_EQ(TimePoint::max(), consensus->startElectionAt);
    EXPECT_EQ(Configuration::State::STAGING, consensus->configuration->state);

    // from leader to new term
    consensus->stepDown(10);
    EXPECT_EQ(0U, consensus->leaderId);
    EXPECT_EQ(0U, consensus->votedFor);
    EXPECT_EQ(Configuration::State::STABLE, consensus->configuration->state);
    EXPECT_LT(Clock::now(), consensus->startElectionAt);
    EXPECT_GT(Clock::now() +
              milliseconds(RaftConsensus::ELECTION_TIMEOUT_MS) * 2,
              consensus->startElectionAt);

    // from candidate to same term
    entry5.set_term(6);
    consensus->append(entry5);
    consensus->startNewElection();
    consensus->leaderId = 3;
    TimePoint oldStartElectionAt = consensus->startElectionAt;
    Clock::mockValue += milliseconds(2);
    consensus->stepDown(consensus->currentTerm);
    EXPECT_NE(0U, consensus->leaderId);
    EXPECT_NE(0U, consensus->votedFor);
    EXPECT_EQ(oldStartElectionAt, consensus->startElectionAt);

    // from follower to new term
    consensus->stepDown(consensus->currentTerm + 1);
    EXPECT_EQ(oldStartElectionAt, consensus->startElectionAt);
}

TEST_F(ServerRaftConsensusTest, updateLogMetadata)
{
    init();
    consensus->stepDown(5);
    consensus->append(entry1);
    consensus->startNewElection();
    consensus->updateLogMetadata();
    EXPECT_EQ(6U, consensus->log->metadata.current_term());
    EXPECT_EQ(1U, consensus->log->metadata.voted_for());
}

// used in upToDateLeader
class UpToDateLeaderHelper {
    explicit UpToDateLeaderHelper(RaftConsensus* consensus)
        : consensus(consensus)
        , iter(1)
    {
    }
    void operator()() {
        Server* server = consensus->configuration->knownServers.at(2).get();
        Peer* peer = dynamic_cast<Peer*>(server);
        if (iter == 1) {
            peer->lastAckEpoch = consensus->currentEpoch;
        } else if (iter == 2) {
            peer->lastAgreeIndex = 4;
            consensus->advanceCommittedId();
        } else {
            FAIL();
        }
        ++iter;
    }
    RaftConsensus* consensus;
    uint64_t iter;
};

TEST_F(ServerRaftConsensusTest, upToDateLeader)
{
    // Log:
    // 1,t5: config { s1 }
    // 2,t6: no op
    // 3,t6: config { s1, s2 }
    // 4,t7: no op
    init();
    std::unique_lock<Mutex> lockGuard(consensus->mutex);
    // not leader -> false
    EXPECT_FALSE(consensus->upToDateLeader(lockGuard));
    consensus->stepDown(5);
    entry1.set_term(5);
    consensus->append(entry1);
    consensus->startNewElection();
    // leader of just self -> true
    EXPECT_EQ(State::LEADER, consensus->state);
    EXPECT_TRUE(consensus->upToDateLeader(lockGuard));
    // leader of non-trivial cluster -> wait, then true
    entry5.set_term(6);
    consensus->append(entry5);
    consensus->startNewElection();
    consensus->becomeLeader();
    Peer* peer = getPeer(2);
    UpToDateLeaderHelper helper(consensus.get());
    consensus->stateChanged.callback = std::ref(helper);
    peer->nextHeartbeatTime = TimePoint::max();
    EXPECT_TRUE(consensus->upToDateLeader(lockGuard));
    EXPECT_EQ(round(Clock::now()),
              peer->nextHeartbeatTime);
    EXPECT_EQ(3U, helper.iter);
}

// This tests an old bug in which nextIndex was not set properly for servers
// that were just added to the configuration.
TEST_F(ServerRaftConsensusTest, regression_nextIndexForNewServer)
{
    // Log:
    // 1,t1: config { s1 }
    // 2,t5: no op
    // 3,t5: config { s1, s2 }
    init();
    consensus->append(entry1);
    consensus->stepDown(4);
    consensus->startNewElection();
    consensus->append(entry5);
    EXPECT_EQ(4U, getPeer(2)->nextIndex);
    EXPECT_TRUE(getPeer(2)->forceHeartbeat);
}


} // namespace LogCabin::Server::<anonymous>
} // namespace LogCabin::Server
} // namespace LogCabin
