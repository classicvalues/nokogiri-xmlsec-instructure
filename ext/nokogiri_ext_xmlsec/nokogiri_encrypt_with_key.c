#include "xmlsecrb.h"
#include "options.h"
#include "util.h"

// Encrypes the XML Document document using XMLEnc.
//
// Expects 3 positional arguments:
//   rb_rsa_key_name - String with name of the rsa key. May be the empty.
//   rb_rsa_key - A PEM encoded rsa key for signing.
//   rb_opts - An ruby hash that configures the encryption options.
//             See XmlEncOptions struct for possible values.
VALUE encrypt_with_key(VALUE self, VALUE rb_rsa_key_name, VALUE rb_rsa_key,
                       VALUE rb_opts) {
  VALUE rb_exception_result = Qnil;
  const char* exception_message = NULL;

  xmlDocPtr doc = NULL;
  xmlNodePtr encDataNode = NULL;
  xmlNodePtr encKeyNode  = NULL;
  xmlNodePtr keyInfoNode = NULL;
  xmlSecEncCtxPtr encCtx = NULL;
  xmlSecKeysMngrPtr keyManager = NULL;
  char *keyName = NULL;
  char *key = NULL;
  unsigned int keyLength = 0;

  resetXmlSecError();

  Check_Type(rb_rsa_key,      T_STRING);
  Check_Type(rb_rsa_key_name, T_STRING);
  Check_Type(rb_opts, T_HASH);

  key       = RSTRING_PTR(rb_rsa_key);
  keyLength = RSTRING_LEN(rb_rsa_key);
  keyName = StringValueCStr(rb_rsa_key_name);

  XmlEncOptions options;
  if (!GetXmlEncOptions(rb_opts, &options, &rb_exception_result,
                        &exception_message)) {
    goto done;
  }

  Data_Get_Struct(self, xmlDoc, doc);

  // create encryption template to encrypt XML file and replace 
  // its content with encryption result
  encDataNode = xmlSecTmplEncDataCreate(doc, options.block_encryption, NULL,
                                        xmlSecTypeEncElement, NULL, NULL);
  if(encDataNode == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to create encryption template";
    goto done;
  }

  // we want to put encrypted data in the <enc:CipherValue/> node
  if(xmlSecTmplEncDataEnsureCipherValue(encDataNode) == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to add CipherValue node";
    goto done;
  }

  // add <dsig:KeyInfo/> and <dsig:KeyName/> nodes to put key name in the
  // signed document
  keyInfoNode = xmlSecTmplEncDataEnsureKeyInfo(encDataNode, NULL);
  if(keyInfoNode == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to add key info";
    goto done;
  }

  if(xmlSecTmplKeyInfoAddKeyName(keyInfoNode, NULL) == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to add key name";
    goto done;
  }

  if ((keyManager = createKeyManagerWithSingleKey(
          key, keyLength, keyName,
          &rb_exception_result,
          &exception_message)) == NULL) {
    // Propagate the exception.
    goto done;
  }

  // create encryption context, we don't need keys manager in this example
  encCtx = xmlSecEncCtxCreate(keyManager);
  if(encCtx == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to create encryption context";
    goto done;
  }

  // Generate the symmetric key.
  encCtx->encKey = xmlSecKeyGenerateByName(BAD_CAST options.key_type, options.key_bits,
                                           xmlSecKeyDataTypeSession);

  if(encCtx->encKey == NULL) {
    rb_exception_result = rb_eDecryptionError;
    exception_message = "failed to generate session key";
    goto done;
  }

  // Set key name.
  if(xmlSecKeySetName(encCtx->encKey, (xmlSecByte *)keyName) < 0) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to set key name";
    goto done;
  }

  // Add <enc:EncryptedKey/> node to the <dsig:KeyInfo/> tag to include
  // the session key.
  encKeyNode = xmlSecTmplKeyInfoAddEncryptedKey(keyInfoNode,
                                       options.key_transport, // encMethodId encryptionMethod
                                       NULL, // xmlChar *idAttribute
                                       NULL, // xmlChar *typeAttribute
                                       NULL  // xmlChar *recipient
                                      );
  if (encKeyNode == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to add encrypted key node";
    goto done;
  }
  if (xmlSecTmplEncDataEnsureCipherValue(encKeyNode) == NULL) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "failed to add encrypted cipher value";
    goto done;
  }

  // encrypt the data
  if(xmlSecEncCtxXmlEncrypt(encCtx, encDataNode, xmlDocGetRootElement(doc)) < 0) {
    rb_exception_result = rb_eEncryptionError;
    exception_message = "encryption failed";
    goto done;
  }
  
  // the template is inserted in the doc, so don't free it
  encDataNode = NULL;
  encKeyNode = NULL;

done:

  /* cleanup */
  if(encCtx != NULL) {
    xmlSecEncCtxDestroy(encCtx);
  }

  if (encKeyNode != NULL) {
    xmlFreeNode(encKeyNode);
  }

  if(encDataNode != NULL) {
    xmlFreeNode(encDataNode);
  }

  if (keyManager != NULL) {
    xmlSecKeysMngrDestroy(keyManager);
  }

  xmlSecErrorsSetCallback(xmlSecErrorsDefaultCallback);

  if(rb_exception_result != Qnil) {
    if (hasXmlSecLastError()) {
      rb_raise(rb_exception_result, "%s, XmlSec error: %s", exception_message,
               getXmlSecLastError());
    } else {
      rb_raise(rb_exception_result, "%s", exception_message);
    }
  }

  return Qnil;
}
