#include "http.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "log.h"

#define BUF_SIZE 4096 * 32

using namespace std;

namespace calvin {

void HttpRequest::Print() const {
  cout << "##### HEADER #######" << endl;
  cout << "Method: " << method_ << endl;
  cout << "URI: " << request_uri_ << endl;
  cout << "Version: " << http_version_ << endl;
  for (auto str : headers_) {
    cout << str << endl;
  }
  cout << content_ << endl;
  cout << "##### END #######" << endl;
}

unique_ptr<HttpRequest> HttpRequest::ParseRequest(int fd,
                                                  HttpRequest::RecvFunc *recv) {
  LOG_INFO("parsing HTTP request (fd: %d)", fd);

  char buffer[BUF_SIZE];
  ssize_t len = recv(fd, buffer, BUF_SIZE - 1, 0);
  if (len < 0) {
    LOG_WARNING("unable to recv request (fd: %d)", fd);
    return unique_ptr<HttpRequest>(nullptr);
  }
  buffer[len] = '\0';

  const char *ptr;
  const char *curr = buffer;
  const char *content = NULL;
  vector<string> headers;

  while ((ptr = strstr(curr, "\r\n")) != NULL) {
    if (ptr - buffer > len) { // Exceeded buffer size.
      LOG_WARNING("buffer overflow (fd: %d)", fd);
      return unique_ptr<HttpRequest>(nullptr);
    }
    if (ptr - curr == 0) { // End of header.
      content = ptr + 2;
      if (headers.size() == 0) {
        LOG_WARNING("empty request? (fd: %d)", fd);
        return unique_ptr<HttpRequest>(nullptr);
      }

      size_t l1 = headers[0].find_first_of(' ');
      string method(headers[0], 0, l1);

      size_t l2 = headers[0].find_first_of(' ', l1 + 1);
      string request_uri(headers[0], l1 + 1, l2 - (l1 + 1));

      string http_version(headers[0], l2 + 1);
      headers.erase(headers.begin());

      size_t content_length = (size_t)(len - (content - buffer));

      LOG_INFO("parsed HTTP request (fd: %d, %s:%s:%s, len: %ld)", fd,
               method.c_str(), request_uri.c_str(), http_version.c_str(),
               content_length);
      return std::unique_ptr<HttpRequest>(new HttpRequest(
          method, request_uri, http_version, headers, content, content_length));
    }
    headers.push_back(string(curr, (size_t)(ptr - curr)));
    curr = ptr + 2;
  }
  LOG_WARNING("invalid request? (fd: %d)", fd);
  return unique_ptr<HttpRequest>(nullptr);
}

void HttpResponse::SendHtmlResponse(const std::string &content) {
  auto type = "text/html";

  LOG_INFO("sending response, MIME: %s, length: %lu", type, content.size());

  stringstream buf;
  buf << "HTTP/1.0 200 OK\r\n";
  buf << "Content-Type:" << type << "\r\n";
  buf << "Content-Length:" << content.size() << "\r\n";
  buf << "Accept-Ranges: bytes"
      << "\r\n";
  buf << "\r\n";
  buf << content;

  send_(fd_, buf.str().c_str(), buf.str().size(), 0);
}

void HttpResponse::SendResponse(const std::string type, int fd, size_t length) {
  LOG_INFO("sending response, MIME: %s, length: %lu, fd: %d", type.c_str(),
           length, fd);

  stringstream ss;
  ss << "HTTP/1.0 200 OK\r\n";
  ss << "Content-Type: " << type << "\r\n";
  ss << "Content-Length: " << length << "\r\n";
  ss << "Accept-Ranges: bytes\r\n";
  ss << "\r\n";
  send_(fd_, ss.str().c_str(), ss.str().size(), 0);

  size_t to_send = length;
  ssize_t available = 1;
  char buffer[BUF_SIZE];
  while (available > 0) {
    available = read(fd, buffer, BUF_SIZE);
    ssize_t sent = send_(fd_, buffer, (size_t)(available), 0);
    if (sent != available) {
      break;
    }
    to_send -= (size_t)sent;
  }
  if (to_send != 0) {
    LOG_WARNING("Finished sending, remaining: %lu, fd: %d", to_send, fd);
  }
}

void HttpResponse::SendPartialResponse(const std::string type, int fd,
                                       size_t length, size_t start,
                                       size_t end) {
  size_t to_send = end - start + 1;
  LOG_INFO("sending response, MIME: %s, length: %lu, fd: %d", type.c_str(),
           length, fd);

  stringstream ss;
  ss << "HTTP/1.0 206 Partial Content\r\n";
  ss << "Content-Type: " << type << "\r\n";
  ss << "Content-Length: " << to_send << "\r\n";
  ss << "Accept-Ranges: bytes\r\n";
  ss << "Content-Range: " << start << "-" << end << "/" << length << "\r\n";
  ss << "\r\n";
  send_(fd_, ss.str().c_str(), ss.str().size(), 0);

  lseek64(fd, (ssize_t)start, SEEK_SET);
  size_t available = 1;
  char buffer[BUF_SIZE];
  while (available > 0 && to_send > 0) {
    available = (size_t)read(fd, buffer, BUF_SIZE);
    size_t sending = (available > to_send) ? to_send : available;
    ssize_t sent = send_(fd_, buffer, sending, 0);
    if (sent != sending) {
      break;
    }
    to_send -= (size_t)sent;
  }

  if (to_send != 0) {
    LOG_WARNING("Finished sending, remaining: %lu, fd: %d", to_send, fd);
  }
}

} // namespace calvin
