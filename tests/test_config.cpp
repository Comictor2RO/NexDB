#include <gtest/gtest.h>
#include "../Config/Config.hpp"
#include <fstream>
#include <cstdio>
#include <stdexcept>

static const char* TEST_CONFIG = "test_config_tmp.json";

static void writeConfig(const std::string &content)
{
    std::ofstream f(TEST_CONFIG);
    f << content;
}

class ConfigTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::remove(TEST_CONFIG);
    }
};

TEST_F(ConfigTest, DefaultsWhenFileIsMissing)
{
    Config cfg = Config::load("nonexistent_config.json");
    EXPECT_EQ(cfg.port,             3000);
    EXPECT_EQ(cfg.page_size,        4096);
    EXPECT_EQ(cfg.cache_capacity,   128);
    EXPECT_EQ(cfg.aux_max_failures, 3);
    EXPECT_EQ(cfg.aux_timeout,      30);
    EXPECT_EQ(cfg.handshake_timeout, 5);

    // A missing config must not hand remote clients CREATE/DROP DATABASE
    EXPECT_FALSE(cfg.allow_remote_db_admin);
}

TEST_F(ConfigTest, LoadsAllFields)
{
    writeConfig(R"({
  "port": 8080,
  "page_size": 4096,
  "cache_capacity": 64,
  "aux_max_failures": 5,
  "aux_timeout": 60,
  "thread_count": 8,
  "handshake_timeout": 10,
  "bypass_localhost": false,
  "allow_remote_db_admin": true,
  "database": "otherdb"
})");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,             8080);
    EXPECT_EQ(cfg.page_size,        4096);
    EXPECT_EQ(cfg.cache_capacity,   64);
    EXPECT_EQ(cfg.aux_max_failures, 5);
    EXPECT_EQ(cfg.aux_timeout,      60);
    EXPECT_EQ(cfg.thread_count,     8);
    EXPECT_EQ(cfg.handshake_timeout, 10);
    EXPECT_FALSE(cfg.bypass_localhost);
    EXPECT_TRUE(cfg.allow_remote_db_admin);
    EXPECT_EQ(cfg.database,         "otherdb");
}

TEST_F(ConfigTest, PartialFileKeepsDefaults)
{
    writeConfig(R"({
  "port": 9000
})");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,             9000);
    EXPECT_EQ(cfg.cache_capacity,   128);
    EXPECT_EQ(cfg.aux_max_failures, 3);
    EXPECT_EQ(cfg.aux_timeout,      30);
}

TEST_F(ConfigTest, EmptyFileKeepsDefaults)
{
    writeConfig("{}");

    Config cfg = Config::load(TEST_CONFIG);
    EXPECT_EQ(cfg.port,           3000);
    EXPECT_EQ(cfg.cache_capacity, 128);
}

TEST_F(ConfigTest, NonNumericValueThrows)
{
    writeConfig(R"({
  "port": "abc"
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

TEST_F(ConfigTest, TrailingGarbageThrows)
{
    writeConfig(R"({
  "port": 123abc
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

TEST_F(ConfigTest, PortOutOfRangeThrows)
{
    writeConfig(R"({
  "port": 99999
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

TEST_F(ConfigTest, NonPositiveValueThrows)
{
    writeConfig(R"({
  "cache_capacity": 0
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

TEST_F(ConfigTest, InvalidBoolThrows)
{
    writeConfig(R"({
  "bypass_localhost": "yes"
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

TEST_F(ConfigTest, AllowRemoteDbAdminDefaultsToBlocked)
{
    writeConfig(R"({
  "port": 9000
})");

    EXPECT_FALSE(Config::load(TEST_CONFIG).allow_remote_db_admin);
}

TEST_F(ConfigTest, AllowRemoteDbAdminAcceptsBothBoolSpellings)
{
    writeConfig(R"({
  "allow_remote_db_admin": true
})");
    EXPECT_TRUE(Config::load(TEST_CONFIG).allow_remote_db_admin);

    writeConfig(R"({
  "allow_remote_db_admin": 1
})");
    EXPECT_TRUE(Config::load(TEST_CONFIG).allow_remote_db_admin);

    writeConfig(R"({
  "allow_remote_db_admin": false
})");
    EXPECT_FALSE(Config::load(TEST_CONFIG).allow_remote_db_admin);

    writeConfig(R"({
  "allow_remote_db_admin": 0
})");
    EXPECT_FALSE(Config::load(TEST_CONFIG).allow_remote_db_admin);
}

TEST_F(ConfigTest, InvalidAllowRemoteDbAdminThrows)
{
    writeConfig(R"({
  "allow_remote_db_admin": "yes"
})");

    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

// A zero or negative timeout would close every handshake on arrival
TEST_F(ConfigTest, NonPositiveHandshakeTimeoutThrows)
{
    writeConfig(R"({
  "handshake_timeout": 0
})");
    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);

    writeConfig(R"({
  "handshake_timeout": -5
})");
    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

// An unknown key aborts the load instead of being ignored, so a typo cannot
// silently leave a security-relevant setting at its default.
TEST_F(ConfigTest, UnknownKeyThrows)
{
    writeConfig(R"({
  "allow_remote_dbadmin": true
})");
    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);

    writeConfig(R"({
  "prot": 3000
})");
    EXPECT_THROW(Config::load(TEST_CONFIG), std::runtime_error);
}

// The message must name the offending key — that is the whole point of rejecting it
TEST_F(ConfigTest, UnknownKeyErrorNamesTheKey)
{
    writeConfig(R"({
  "nonsense_key": 1
})");

    try {
        Config::load(TEST_CONFIG);
        FAIL() << "expected Config::load to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("nonsense_key"), std::string::npos) << e.what();
    }
}

// Guards the strict-key check against its own failure mode: braces and blank lines
// are not keys, and neither is a key whose value is empty.
TEST_F(ConfigTest, StructuralLinesAreNotTreatedAsKeys)
{
    writeConfig("{\n\n}\n");
    EXPECT_NO_THROW(Config::load(TEST_CONFIG));

    writeConfig("{\n  \"port\":\n}\n");
    EXPECT_NO_THROW(Config::load(TEST_CONFIG));
}

// The shipped config.json must load, or the application cannot start. Skipped rather
// than passed vacuously when the working directory is not the repository root, since a
// missing file makes Config::load return defaults without throwing.
TEST_F(ConfigTest, ShippedConfigIsAccepted)
{
    if (!std::ifstream("config.json").is_open())
        GTEST_SKIP() << "config.json is not in the working directory";

    EXPECT_NO_THROW(Config::load("config.json"));
}
