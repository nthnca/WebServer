#include "http.h"

#include <cstdio>
#include <fstream>
#include <iostream>

#include "log.h"
#include "gtest/gtest.h"

using namespace std;

#define BUF_SIZE 4096 * 32

// Test Helpers
ssize_t file_writer(int fd, const void *buf, size_t bytes, int options) {
  return write(fd, buf, bytes);
}

ssize_t file_reader(int fd, void *buf, size_t bytes, int) {
  return read(fd, buf, bytes);
}

void CompareFileToString(const string &filename, const string &compare) {
  FILE *fd = fopen(filename.c_str(), "r");
  if (fd == nullptr) {
    exit(1);
  }

  char buf[BUF_SIZE];
  auto bytes = read(fileno(fd), buf, BUF_SIZE);

  string str(buf, bytes);
  EXPECT_EQ(compare, string(buf, bytes));
}

FILE *GetSrcFileForTest(const char *data, const char *tmp_filename) {
  ofstream out_file;

  out_file.open(tmp_filename, ios_base::trunc);
  if (!out_file.is_open()) {
    exit(1);
  }
  out_file << data;
  out_file.close();

  FILE *fd = fopen(tmp_filename, "r");
  if (fd == nullptr) {
    exit(1);
  }

  return fd;
}

FILE *GetSnkFileForTest(const char *tmp_filename) {
  FILE *fd = fopen(tmp_filename, "w");
  if (fd == nullptr) {
    exit(1);
  }

  return fd;
}

TEST(HttpTest, ParseRequest1Success) {
  auto *fd = GetSrcFileForTest("GET / HTTP/1.1\r\n"
                               "Host: 192.168.86.2\r\n"
                               "User-Agent: curl/7.52.1\r\n"
                               "Accept: */*\r\n"
                               "\r\n",
                               "/tmp/httptest_" FILE_AND_LINE);

  auto r = calvin::HttpRequest::ParseRequest(fileno(fd), file_reader);

  EXPECT_EQ("GET", r->Method());
  EXPECT_EQ("/", r->RequestUri());
  EXPECT_EQ((vector<string>{"Host: 192.168.86.2", "User-Agent: curl/7.52.1",
                            "Accept: */*"}),
            r->Headers());
  EXPECT_EQ("", r->Content());
  fclose(fd);
}

TEST(HttpTest, ParseRequest2Success) {
  auto *fd = GetSrcFileForTest(
      "GET /hello.htm HTTP/1.1\r\n"
      "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n"
      "Accept-Language: en-us\r\n"
      "Accept-Encoding: gzip, deflate\r\n"
      "Connection: Keep-Alive\r\n"
      "\r\n",
      "/tmp/httptest_" FILE_AND_LINE);
  auto r = calvin::HttpRequest::ParseRequest(fileno(fd), file_reader);

  EXPECT_EQ("GET", r->Method());
  EXPECT_EQ("/hello.htm", r->RequestUri());
  EXPECT_EQ((vector<string>{
                "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)",
                "Accept-Language: en-us", "Accept-Encoding: gzip, deflate",
                "Connection: Keep-Alive"}),
            r->Headers());
  EXPECT_EQ("", r->Content());
  fclose(fd);
}

TEST(HttpTest, ParseRequestFailure) {
  const auto vec = std::vector<std::string>{
      "",
      "\r\n",
      "\r\n\r\n",
      "asdfasdfasdfasdfasdfasdf",
      "\r\nasdfasdfasdf",
      "\r\n\r\n",
  };
  for (auto str : vec) {
    auto *fd = GetSrcFileForTest(str.c_str(), "/tmp/httptest_" FILE_AND_LINE);
    auto r = calvin::HttpRequest::ParseRequest(fileno(fd), file_reader);

    EXPECT_FALSE(r);
    fclose(fd);
  }
}

TEST(HttpTest, SendResponseSuccess) {
  string filename("/tmp/httptest_" FILE_AND_LINE);
  FILE *snk = GetSnkFileForTest(filename.c_str());
  auto resp = calvin::HttpResponse(fileno(snk), file_writer);
  string payload(
      "GET /hello.htm HTTP/1.1\r\n"
      "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n"
      "Accept-Language: en-us\r\n"
      "Accept-Encoding: gzip, deflate\r\n"
      "Connection: Keep-Alive\r\n");
  FILE *src =
      GetSrcFileForTest(payload.c_str(), "/tmp/httptest_" FILE_AND_LINE);

  resp.SendResponse("type", fileno(src), 2);
  fclose(src);
  fclose(snk);

  CompareFileToString(filename, string("HTTP/1.0 200 OK\r\n"
                                       "Content-Type: type\r\n"
                                       "Content-Length: 2\r\n"
                                       "Accept-Ranges: bytes\r\n"
                                       "\r\n") +
                                    payload);
}

TEST(HttpTest, SendPartialResponseSuccess) {
  string filename("/tmp/httptest_" FILE_AND_LINE);
  FILE *snk = GetSnkFileForTest(filename.c_str());
  auto resp = calvin::HttpResponse(fileno(snk), file_writer);
  string payload("a1a2a3a4a5a6a7a8a9a0"
                 "b1b2b3b4b5b6b7b8b9b0"
                 "c1c2c3c4c5c6c7c8c9c0"
                 "d1d2d3d4d5d6d7d8d9d0"
                 "e1e2e3e4e5e6e7e8e9e0");
  FILE *src =
      GetSrcFileForTest(payload.c_str(), "/tmp/httptest_" FILE_AND_LINE);

  resp.SendPartialResponse("type", fileno(src), 16, 2, 8);
  fclose(src);
  fclose(snk);

  CompareFileToString(filename, string("HTTP/1.0 206 Partial Content\r\n"
                                       "Content-Type: type\r\n"
                                       "Content-Length: 7\r\n"
                                       "Accept-Ranges: bytes\r\n"
                                       "Content-Range: 2-8/16\r\n"
                                       "\r\n") +
                                    "a2a3a4a");
}
