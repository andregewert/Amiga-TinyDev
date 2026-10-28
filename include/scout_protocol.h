/**
 * @file scout_protocol.h
 * @brief Numbers of the Scout protocol shared by Scout and its parsers:
 *        protocol version, symbol kinds, diagnostic severities, token
 *        kinds and Scout's own error codes.
 *
 * The protocol is described in src/scout/docs/Scout-Protocol.md. No Amiga
 * headers, so parsers and host tests can use it.
 */
#ifndef TINYDEV_SCOUT_PROTOCOL_H
#define TINYDEV_SCOUT_PROTOCOL_H

/** The version of the protocol ("protocol" of scout/describe). */
#define SCOUT_PROTOCOL_VERSION 1

/**
 * @name Symbol kinds
 * 1 to 26 are the numbers of the LSP SymbolKind; from 100 on, kinds LSP
 * lacks.
 */
/** @{ */
#define SCOUT_SYMBOL_FILE           1   /**< a file */
#define SCOUT_SYMBOL_MODULE         2   /**< a module */
#define SCOUT_SYMBOL_NAMESPACE      3   /**< a namespace */
#define SCOUT_SYMBOL_PACKAGE        4   /**< a package */
#define SCOUT_SYMBOL_CLASS          5   /**< a class */
#define SCOUT_SYMBOL_METHOD         6   /**< a method */
#define SCOUT_SYMBOL_PROPERTY       7   /**< a property */
#define SCOUT_SYMBOL_FIELD          8   /**< a field of a struct or union */
#define SCOUT_SYMBOL_CONSTRUCTOR    9   /**< a constructor */
#define SCOUT_SYMBOL_ENUM           10  /**< an enumeration */
#define SCOUT_SYMBOL_INTERFACE      11  /**< an interface */
#define SCOUT_SYMBOL_FUNCTION       12  /**< a function */
#define SCOUT_SYMBOL_VARIABLE       13  /**< a variable or parameter */
#define SCOUT_SYMBOL_CONSTANT       14  /**< a constant */
#define SCOUT_SYMBOL_STRING         15  /**< a string */
#define SCOUT_SYMBOL_NUMBER         16  /**< a number */
#define SCOUT_SYMBOL_BOOLEAN        17  /**< a boolean */
#define SCOUT_SYMBOL_ARRAY          18  /**< an array */
#define SCOUT_SYMBOL_OBJECT         19  /**< an object */
#define SCOUT_SYMBOL_KEY            20  /**< a key */
#define SCOUT_SYMBOL_NULL           21  /**< null */
#define SCOUT_SYMBOL_ENUM_MEMBER    22  /**< a member of an enumeration */
#define SCOUT_SYMBOL_STRUCT         23  /**< a struct or union */
#define SCOUT_SYMBOL_EVENT          24  /**< an event */
#define SCOUT_SYMBOL_OPERATOR       25  /**< an operator */
#define SCOUT_SYMBOL_TYPE_PARAMETER 26  /**< a type parameter */
#define SCOUT_SYMBOL_MACRO          100 /**< a macro */
#define SCOUT_SYMBOL_TYPE_ALIAS     101 /**< a type alias (typedef) */
#define SCOUT_SYMBOL_LABEL          102 /**< a label */
#define SCOUT_SYMBOL_PROCEDURE      103 /**< a procedure */
/** @} */

/**
 * @name Diagnostic severities
 * The numbers of the LSP DiagnosticSeverity.
 */
/** @{ */
#define SCOUT_SEVERITY_ERROR        1   /**< an error */
#define SCOUT_SEVERITY_WARNING      2   /**< a warning */
#define SCOUT_SEVERITY_INFO         3   /**< information */
#define SCOUT_SEVERITY_HINT         4   /**< a hint */
/** @} */

/** @name Token kinds (highlighting tokens of scout/analyze) */
/** @{ */
#define SCOUT_TOKEN_TYPE            1   /**< a type (struct, typedef, ...) */
#define SCOUT_TOKEN_FUNCTION        2   /**< a function or procedure */
#define SCOUT_TOKEN_MACRO           3   /**< a macro */
#define SCOUT_TOKEN_CONSTANT        4   /**< a constant (enum member, ...) */
#define SCOUT_TOKEN_VARIABLE        5   /**< a variable */
#define SCOUT_TOKEN_PARAMETER       6   /**< a parameter */
#define SCOUT_TOKEN_FIELD           7   /**< a field */
#define SCOUT_TOKEN_LABEL           8   /**< a label */
#define SCOUT_TOKEN_MODULE          9   /**< a module or namespace */
/** @} */

/**
 * @name Error codes of the protocol
 * Besides the JSON-RPC codes of scout_rpc.h.
 */
/** @{ */
#define SCOUT_ERR_FILE              -32001  /**< the file cannot be read */
#define SCOUT_ERR_LANGUAGE          -32002  /**< language not handled by the parser */
#define SCOUT_ERR_MEMORY            -32003  /**< out of memory */
/** @} */

#endif
