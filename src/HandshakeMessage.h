/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#ifndef _D_HANDSHAKE_MESSAGE_H_
#define _D_HANDSHAKE_MESSAGE_H_

#include "SimplePeerMessage.h"

#define PSTR "BitTorrent protocol"
#define HANDSHAKE_MESSAGE_LENGTH 68

class HandshakeMessage : public SimplePeerMessage {
private:
  char msg[HANDSHAKE_MESSAGE_LENGTH];
  void init();
public:
  char pstrlen;
  string pstr;
  unsigned char reserved[8];
  unsigned char infoHash[INFO_HASH_LENGTH];
  char peerId[PEER_ID_LENGTH];
public:
  HandshakeMessage();
  HandshakeMessage(const unsigned char* infoHash, const char* peerId);

  static HandshakeMessage* create(const char* data, int dataLength);

  virtual ~HandshakeMessage() {}

  virtual int getId() const { return 999; }
  virtual void receivedAction() {};
  virtual const char* getMessage();
  virtual int getMessageLength();
  virtual void check() const;
  virtual string toString() const;

  bool isFastExtensionSupported() const;
};

#endif // _D_HANDSHAKE_MESSAGE_H_
