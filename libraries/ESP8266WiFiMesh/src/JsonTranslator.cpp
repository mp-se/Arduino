/*
 * Copyright (C) 2019 Anders Löfgren
 *
 * License (MIT license):
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "JsonTranslator.h"
#include "EspnowProtocolInterpreter.h"
#include "TypeConversionFunctions.h"
#include "MeshCryptoInterface.h"

namespace TypeCast = MeshTypeConversionFunctions;

namespace JsonTranslator
{
  String createJsonPair(const String &valueIdentifier, const String &value)
  {
    return valueIdentifier + '\"' + value + F("\",");
  }

  String createJsonEndPair(const String &valueIdentifier, const String &value)
  {
    return valueIdentifier + '\"' + value + F("\"}}");
  }
  
  String createEncryptedConnectionInfo(const String &infoHeader, const String &requestNonce, const String &authenticationPassword, const uint64_t ownSessionKey, const uint64_t peerSessionKey)
  {
    // Returns: Encrypted connection info:{"arguments":{"nonce":"1F2","password":"abc","ownSK":"3B4","peerSK":"1A2"}}

    return
    infoHeader + String(F("{\"arguments\":{")) 
    + createJsonPair(FPSTR(jsonNonce), requestNonce)
    + createJsonPair(FPSTR(jsonPassword), authenticationPassword)
    + createJsonPair(FPSTR(jsonOwnSessionKey), TypeCast::uint64ToString(peerSessionKey))   // Exchanges session keys since it should be valid for the receiver.
    + createJsonEndPair(FPSTR(jsonPeerSessionKey), TypeCast::uint64ToString(ownSessionKey));
  }
  
  String createEncryptionRequestIntro(const String &requestHeader, const uint32_t duration)
  {
    return 
    requestHeader + String(F("{\"arguments\":{")) 
    + (requestHeader == FPSTR(EspnowProtocolInterpreter::temporaryEncryptionRequestHeader) ? createJsonPair(FPSTR(jsonDuration), String(duration)) : emptyString);
  }
  
  String createEncryptionRequestEnding(const String &requestNonce)
  {
    return createJsonEndPair(FPSTR(jsonNonce), requestNonce);
  }
  
  String createEncryptionRequestHmacMessage(const String &requestHeader, const String &requestNonce, const uint8_t *hashKey, const uint8_t hashKeyLength, const uint32_t duration)
  {
    String mainMessage = createEncryptionRequestIntro(requestHeader, duration) + createJsonPair(FPSTR(jsonNonce), requestNonce);
    uint8_t staMac[6] {0};
    uint8_t apMac[6] {0};
    String requesterStaApMac = TypeCast::macToString(WiFi.macAddress(staMac)) + TypeCast::macToString(WiFi.softAPmacAddress(apMac));
    String hmac = MeshCryptoInterface::createMeshHmac(requesterStaApMac + mainMessage, hashKey, hashKeyLength);
    return mainMessage + createJsonEndPair(FPSTR(jsonHmac), hmac);
  }

  bool verifyEncryptionRequestHmac(const String &encryptionRequestHmacMessage, const uint8_t *requesterStaMac, const uint8_t *requesterApMac, 
                                   const uint8_t *hashKey, const uint8_t hashKeyLength)
  {
    using MeshCryptoInterface::verifyMeshHmac;
    
    String hmac;
    if(getHmac(encryptionRequestHmacMessage, hmac))
    {
      int32_t hmacStartIndex = encryptionRequestHmacMessage.indexOf(FPSTR(jsonHmac));
      if(hmacStartIndex < 0)
        return false;
     
      if(hmac.length() == 2*CryptoInterface::SHA256_NATURAL_LENGTH // We know that each HMAC byte should become 2 String characters due to uint8ArrayToHexString.
         && verifyMeshHmac(TypeCast::macToString(requesterStaMac) + TypeCast::macToString(requesterApMac) + encryptionRequestHmacMessage.substring(0, hmacStartIndex), hmac, hashKey, hashKeyLength))
      {
        return true;
      }
    }

    return false;
  }
  
  int32_t getStartIndex(const String &jsonString, const String &valueIdentifier, const int32_t searchStartIndex)
  {
    int32_t startIndex = jsonString.indexOf(valueIdentifier, searchStartIndex);
    if(startIndex < 0)
      return startIndex;
  
    startIndex += valueIdentifier.length() + 1; // Do not include valueIdentifier and initial quotation mark
    return startIndex;
  }
  
  int32_t getEndIndex(const String &jsonString, const int32_t searchStartIndex)
  {
    int32_t endIndex = jsonString.indexOf(',', searchStartIndex);
    if(endIndex < 0)
      endIndex = jsonString.indexOf('}', searchStartIndex);
  
    endIndex -= 1; // End index will be at the character after the closing quotation mark, so need to subtract 1.
  
    return endIndex;
  }

  bool getConnectionState(const String &jsonString, String &result)
  {
    int32_t startIndex = jsonString.indexOf(FPSTR(jsonConnectionState));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = jsonString.indexOf('}');
    if(endIndex < 0)
      return false;
      
    result = jsonString.substring(startIndex, endIndex + 1);
    return true;
  }
  
  bool getPassword(const String &jsonString, String &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonPassword));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0)
      return false;
  
    result = jsonString.substring(startIndex, endIndex);
    return true;
  }
  
  bool getOwnSessionKey(const String &jsonString, uint64_t &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonOwnSessionKey));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0)
      return false;
  
    result = TypeCast::stringToUint64(jsonString.substring(startIndex, endIndex));
    return true;
  }
  
  bool getPeerSessionKey(const String &jsonString, uint64_t &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonPeerSessionKey));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0)
      return false;
    
    result = TypeCast::stringToUint64(jsonString.substring(startIndex, endIndex));
    return true;
  }
  
  bool getPeerStaMac(const String &jsonString, uint8_t *resultArray)
  {  
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonPeerStaMac));
    if(startIndex < 0)
      return false;
  
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0 || endIndex - startIndex != 12) // Mac String is always 12 characters long
      return false;
    
    TypeCast::stringToMac(jsonString.substring(startIndex, endIndex), resultArray);
    return true;
  }
  
  bool getPeerApMac(const String &jsonString, uint8_t *resultArray)
  {  
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonPeerApMac));
    if(startIndex < 0)
      return false;
      
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0 || endIndex - startIndex != 12) // Mac String is always 12 characters long
      return false;
    
    TypeCast::stringToMac(jsonString.substring(startIndex, endIndex), resultArray);
    return true;
  }
  
  bool getDuration(const String &jsonString, uint32_t &result)
  {  
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonDuration));
    if(startIndex < 0)
      return false;
    
    result = strtoul(jsonString.substring(startIndex).c_str(), nullptr, 0); // strtoul stops reading input when an invalid character is discovered.
    return true;
  }
  
  bool getNonce(const String &jsonString, String &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonNonce));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0)
      return false;
      
    result = jsonString.substring(startIndex, endIndex);
    return true;
  }

  bool getHmac(const String &jsonString, String &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonHmac));
    if(startIndex < 0)
      return false;
    
    int32_t endIndex = getEndIndex(jsonString, startIndex);
    if(endIndex < 0)
      return false;
      
    result = jsonString.substring(startIndex, endIndex);
    return true;
  }

  bool getDesync(const String &jsonString, bool &result)
  {  
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonDesync));
    if(startIndex < 0)
      return false;
    
    result = bool(strtoul(jsonString.substring(startIndex).c_str(), nullptr, 0)); // strtoul stops reading input when an invalid character is discovered.
    return true;
  }

  bool getUnsynchronizedMessageID(const String &jsonString, uint32_t &result)
  {
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonUnsynchronizedMessageID));
    if(startIndex < 0)
      return false;
    
    result = strtoul(jsonString.substring(startIndex).c_str(), nullptr, 0); // strtoul stops reading input when an invalid character is discovered.
    return true;
  }

  bool getMeshMessageCount(const String &jsonString, uint16_t &result)
  {  
    int32_t startIndex = getStartIndex(jsonString, FPSTR(jsonMeshMessageCount));
    if(startIndex < 0)
      return false;

    uint32_t longResult = strtoul(jsonString.substring(startIndex).c_str(), nullptr, 0); // strtoul stops reading input when an invalid character is discovered.
    assert(longResult <= 65535); // Must fit within uint16_t
    
    result = longResult;
    return true;
  }
}
