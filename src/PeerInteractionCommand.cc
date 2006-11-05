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
#include "PeerInteractionCommand.h"
#include "PeerInitiateConnectionCommand.h"
#include "PeerMessageUtil.h"
#include "HandshakeMessage.h"
#include "KeepAliveMessage.h"
#include "ChokeMessage.h"
#include "UnchokeMessage.h"
#include "HaveMessage.h"
#include "DlAbortEx.h"
#include "Util.h"
#include "message.h"
#include "prefs.h"
#include <algorithm>

PeerInteractionCommand::PeerInteractionCommand(int cuid,
					       const PeerHandle& p,
					       TorrentDownloadEngine* e,
					       const BtContextHandle& btContext,
					       const SocketHandle& s,
					       int sequence)
  :PeerAbstractCommand(cuid, p, e, btContext, s), sequence(sequence) {
  if(sequence == INITIATOR_SEND_HANDSHAKE) {
    disableReadCheckSocket();
    setWriteCheckSocket(socket);
    setTimeout(e->option->getAsInt(PREF_PEER_CONNECTION_TIMEOUT));
  }
  peerInteraction = new PeerInteraction(cuid, peer, socket, e->option,
					btContext);
  setUploadLimit(e->option->getAsInt(PREF_MAX_UPLOAD_LIMIT));
  chokeUnchokeCount = 0;
  haveCount = 0;
  keepAliveCount = 0;
  peer->activate();
}

PeerInteractionCommand::~PeerInteractionCommand() {
  delete peerInteraction;
  peer->deactivate();
}

bool PeerInteractionCommand::executeInternal() {
  disableWriteCheckSocket();
  setUploadLimitCheck(false);
  setNoCheck(false);

  switch(sequence) {
  case INITIATOR_SEND_HANDSHAKE:
    if(!socket->isWritable(0)) {
      setWriteCheckSocket(socket);
      break;
    }
    socket->setBlockingMode();
    setReadCheckSocket(socket);
    setTimeout(e->option->getAsInt(PREF_TIMEOUT));
    peerInteraction->sendHandshake();
    sequence = INITIATOR_WAIT_HANDSHAKE;
    break;
  case INITIATOR_WAIT_HANDSHAKE: {
    if(peerInteraction->countMessageInQueue() > 0) {
      peerInteraction->sendMessages();
      if(peerInteraction->countMessageInQueue() > 0) {
	break;
      }
    }
    PeerMessageHandle handshakeMessage =
      peerInteraction->receiveHandshake();
    if(handshakeMessage.get() == 0) {
      break;
    }
    peer->setPeerId(((HandshakeMessage*)handshakeMessage.get())->peerId);
    logger->info(MSG_RECEIVE_PEER_MESSAGE, cuid,
		 peer->ipaddr.c_str(), peer->port,
		 handshakeMessage->toString().c_str());
    haveCheckTime.reset();
    peerInteraction->sendBitfield();
    peerInteraction->sendAllowedFast();
    sequence = WIRED;
    break;
  }
  case RECEIVER_WAIT_HANDSHAKE: {
    PeerMessageHandle handshakeMessage =
      peerInteraction->receiveHandshake(true);
    if(handshakeMessage.get() == 0) {
      break;
    }
    peer->setPeerId(((HandshakeMessage*)handshakeMessage.get())->peerId);
    logger->info(MSG_RECEIVE_PEER_MESSAGE, cuid,
		 peer->ipaddr.c_str(), peer->port,
		 handshakeMessage->toString().c_str());
    haveCheckTime.reset();
    peerInteraction->sendBitfield();
    peerInteraction->sendAllowedFast();
    sequence = WIRED;    
    break;
  }
  case WIRED:
    peerInteraction->syncPiece();
    decideChoking();

    if(periodicExecPoint.elapsedInMillis(500)) {
      periodicExecPoint.reset();
      detectMessageFlooding();
      peerInteraction->checkRequestSlot();
      checkHave();
      sendKeepAlive();
    }
    receiveMessages();

    peerInteraction->addRequests();

    peerInteraction->sendMessages();
    break;
  }
  if(peerInteraction->countMessageInQueue() > 0) {
    if(peerInteraction->isSendingMessageInProgress()) {
      setUploadLimitCheck(true);
    }
    setNoCheck(true);
  }
  e->commands.push_back(this);
  return false;
}

#define FLOODING_CHECK_INTERVAL 5

void PeerInteractionCommand::detectMessageFlooding() {
  if(freqCheckPoint.elapsed(FLOODING_CHECK_INTERVAL)) {
    if(chokeUnchokeCount*1.0/FLOODING_CHECK_INTERVAL >= 0.4
       //|| haveCount*1.0/elapsed >= 20.0
       || keepAliveCount*1.0/FLOODING_CHECK_INTERVAL >= 1.0) {
      throw new DlAbortEx("Flooding detected.");
    } else {
      chokeUnchokeCount = 0;
      haveCount = 0;
      keepAliveCount = 0;
      freqCheckPoint.reset();
    }
  }
}

/*
void PeerInteractionCommand::checkLongTimePeerChoking() {
  if(pieceStorage->downloadFinished()) {
    return;
  }    
  if(peer->amInterested && peer->peerChoking) {
    if(chokeCheckPoint.elapsed(MAX_PEER_CHOKING_INTERVAL)) {
      logger->info("CUID#%d - The peer is choking too long.", cuid);
      peer->snubbing = true;
    }
  } else {
    chokeCheckPoint.reset();
  }
}
*/

void PeerInteractionCommand::decideChoking() {
  if(peer->shouldBeChoking()) {
    if(!peer->amChoking) {
      peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				  createChokeMessage());
    }
  } else {
    if(peer->amChoking) {
      peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				  createUnchokeMessage());
    }
  }
}

void PeerInteractionCommand::receiveMessages() {
  for(int i = 0; i < 50; i++) {
    int maxSpeedLimit = e->option->getAsInt(PREF_MAX_DOWNLOAD_LIMIT);
    if(maxSpeedLimit > 0) {
      TransferStat stat = peerStorage->calculateStat();
      if(maxSpeedLimit < stat.downloadSpeed) {
	disableReadCheckSocket();
	setNoCheck(true);
	break;
      }
    }

    PeerMessageHandle message = peerInteraction->receiveMessage();
    if(message.get() == NULL) {
      return;
    }
    logger->info(MSG_RECEIVE_PEER_MESSAGE, cuid,
		 peer->ipaddr.c_str(), peer->port,
		 message->toString().c_str());
    // to detect flooding
    switch(message->getId()) {
    case KeepAliveMessage::ID:
      keepAliveCount++;
      break;
    case ChokeMessage::ID:
      if(!peer->peerChoking) {
	chokeUnchokeCount++;
      }
      break;
    case UnchokeMessage::ID:
      if(peer->peerChoking) {
	chokeUnchokeCount++;
      }
      break;
    case HaveMessage::ID:
      haveCount++;
      break;
    }
    message->receivedAction();
  }
}

// TODO this method removed when PeerBalancerCommand is implemented
bool PeerInteractionCommand::prepareForNextPeer(int wait) {
  if(peerStorage->isPeerAvailable() && btRuntime->lessThanEqMinPeer()) {
    PeerHandle peer = peerStorage->getUnusedPeer();
    int newCuid = btRuntime->getNewCuid();
    peer->cuid = newCuid;
    PeerInitiateConnectionCommand* command =
      new PeerInitiateConnectionCommand(newCuid,
					peer,
					e,
					btContext);
    e->commands.push_back(command);
  }
  return true;
}

bool PeerInteractionCommand::prepareForRetry(int wait) {
  e->commands.push_back(this);
  return false;
}

void PeerInteractionCommand::onAbort(Exception* ex) {
  peerInteraction->abortAllPieces();
  PeerAbstractCommand::onAbort(ex);
}

void PeerInteractionCommand::sendKeepAlive() {
  if(keepAliveCheckPoint.elapsed(KEEP_ALIVE_INTERVAL)) {
    if(peerInteraction->countMessageInQueue() == 0) {
      peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				  createKeepAliveMessage());
      peerInteraction->sendMessages();
    }
    keepAliveCheckPoint.reset();
  }
}

void PeerInteractionCommand::checkHave() {
  Integers indexes =
    pieceStorage->getAdvertisedPieceIndexes(cuid, haveCheckTime);
  haveCheckTime.reset();
  if(indexes.size() >= 20) {
    if(peer->isFastExtensionEnabled()) {
      if(pieceStorage->downloadFinished()) {
	peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				    createHaveAllMessage());
      } else {
	peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				    createBitfieldMessage());
      }
    } else {
      peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				  createBitfieldMessage());
    }
  } else {
    for(Integers::iterator itr = indexes.begin(); itr != indexes.end(); itr++) {
      peerInteraction->addMessage(peerInteraction->getPeerMessageFactory()->
				  createHaveMessage(*itr));
    }
  }
}
