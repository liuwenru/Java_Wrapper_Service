/*
 * Copyright (c) 1999, 2020 Tanuki Software, Ltd.
 * http://www.tanukisoftware.comment
 * All rights reserved.
 *
 * This software is the proprietary information of Tanuki Software.
 * You shall use it only in accordance with the terms of the
 * license agreement you entered into with Tanuki Software.
 * http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html
 *
 *
 * Portions of the Software have been derived from source code
 * developed by Silver Egg Technology under the following license:
 *
 * Copyright (c) 2001 Silver Egg Technology
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sub-license, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
 #include <errno.h>

/* MS Visual Studio 8 went and deprecated the POXIX names for functions.
 *  Fixing them all would be a big headache for UNIX versions. */
 #pragma warning(disable : 4996)

#else
 #include <strings.h>
 #include <limits.h>
 #include <sys/time.h>
 #include <langinfo.h>
#endif
#include "wrapper_i18n.h"
#include "logger.h"
#include "logger_file.h"
#include "property.h"
#include "wrapper.h"
#include "wrapper_file.h"

#define MAX_INCLUDE_DEPTH 10

/* The largest possible "name+'='+value" property pair length on Windows. */
#define MAX_ENV_PAIR_LEN 32767

EnvSrc *baseEnvSrc = NULL;

/** Stores the time that the property file began to be loaded. */
struct tm loadPropertiesTM;

const TCHAR **escapedPropertyNames = NULL;
void setInnerProperty(Properties *properties, Property *property, const TCHAR *propertyValue, int warnUndefinedVars);

/**
 * @param warnUndefinedVars Log warnings about missing environment variables.
 */
void prepareProperty(Properties *properties, Property *property, int warnUndefinedVars) {
    TCHAR *oldValue;

    if (_tcsstr(property->value_inner, TEXT("%"))) {
        /* Reset the property.  If the unreplaced environment variables are now available
         *  setting it again will cause it to be replaced correctly.  If not this will
         *  only waste time.  The value will be freed in the process so we need to
         *  keep it around. */
#ifdef _DEBUG
        _tprintf( TEXT("Unreplaced property %s=%s\n"), property->name, property->value_inner );
#endif
        oldValue = malloc(sizeof(TCHAR) * (_tcslen(property->value_inner) + 1));
        if (!oldValue) {
            outOfMemory(TEXT("PP"), 1);
        } else {
            _tcsncpy(oldValue, property->value_inner, _tcslen(property->value_inner) + 1);
            setInnerProperty(properties, property, oldValue, warnUndefinedVars);
            free(oldValue);
        }
#ifdef _DEBUG
        _tprintf( TEXT("        -> property %s=%s\n"), property->name, property->value_inner );
#endif
    }
}

/**
 * Private function to find a Property structure.
 */
Property* getInnerProperty(Properties *properties, const TCHAR *propertyName, int warnUndefinedVars) {
    Property *property;
    int cmp;

    /* Loop over the properties which are in order and look for the specified property. */
    property = properties->first;
    while (property != NULL) {
        cmp = strcmpIgnoreCase(property->name, propertyName);
        if (cmp > 0) {
            /* This property would be after the one being looked for, so it does not exist. */
            return NULL;
        } else if (cmp == 0) {
            /* We found it. */
            prepareProperty(properties, property, warnUndefinedVars && properties->logWarnings);
            
            return property;
        }
        /* Keep looking */
        property = property->next;
    }
    /* We did not find the property being looked for. */
    return NULL;
}

void addInnerProperty(Properties *properties, Property *newProperty) {
    newProperty->previous = properties->last;
    if (properties->last == NULL) {
        /* This will be the first property. */
        properties->first = newProperty;
    } else {
        /* Point the old last property to the new last property. */
        properties->last->next = newProperty;
    }
    properties->last = newProperty;
    newProperty->next = NULL;
}

void insertInnerProperty(Properties *properties, Property *newProperty) {
    Property *property;
    int cmp;

    /* Loop over the properties which are in order and look for the specified property. */
    /* This function assumes that Property is not already in properties. */
    property = properties->first;
    while (property != NULL) {
        cmp = strcmpIgnoreCase(property->name, newProperty->name);
        if (cmp > 0) {
            /* This property would be after the new property, so insert it here. */
            newProperty->previous = property->previous;
            newProperty->next = property;
            if (property->previous == NULL) {
                /* This was the first property */
                properties->first = newProperty;
            } else {
                property->previous->next = newProperty;
            }
            property->previous = newProperty;

            /* We are done, so return */
            return;
        }

        property = property->next;
    }

    /* The new property needs to be added at the end */
    addInnerProperty(properties, newProperty);
}

Property* createInnerProperty() {
    Property *property;

    property = malloc(sizeof(Property));
    if (!property) {
        outOfMemory(TEXT("CIP"), 1);
        return NULL;
    }
    property->name = NULL;
    property->next = NULL;
    property->previous = NULL;
    property->value = NULL;
    property->value_inner = NULL;
    property->filePath = NULL;
    property->lineNumber = 0;
    property->definitions = 1;
    property->isGenerated = FALSE;
    property->isVariable = FALSE;
    property->lastDefinitionDepth = -1;

    return property;
}

/**
 * Private function to dispose a Property structure.  Assumes that the
 *    Property is disconnected already.
 */
void disposeInnerProperty(Property *property) {
    if (property->name) {
        free(property->name);
    }
    if (property->value) {
        /* The memory may contain sensitive data or passwords and must be erased. */
        wrapperSecureFreeStrW(property->value);
    }
    if (property->value_inner) {
        /* The memory may contain sensitive data or passwords and must be erased. */
        wrapperSecureFreeStrW(property->value_inner);
    }
    if (property->filePath) {
        free(property->filePath);
    }
    free(property);
}

TCHAR generateValueBuffer[256];

/**
 * This function returns a reference to a static buffer and is NOT thread safe.
 *  It is currently called only when loading a property file and when firing an event.
 *  Both happen in the main thread.
 * The largest return value can be 15+1 characters.
 */
TCHAR* generateTimeValue(const TCHAR* format, struct tm *timeTM) {
    if (strcmpIgnoreCase(format, TEXT("YYYYMMDDHHIISS")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d%02d%02d%02d%02d"),
        timeTM->tm_year + 1900, timeTM->tm_mon + 1, timeTM->tm_mday,
        timeTM->tm_hour, timeTM->tm_min, timeTM->tm_sec );
    } else if (strcmpIgnoreCase(format, TEXT("YYYYMMDD_HHIISS")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d%02d_%02d%02d%02d"),
        timeTM->tm_year + 1900, timeTM->tm_mon + 1, timeTM->tm_mday,
        timeTM->tm_hour, timeTM->tm_min, timeTM->tm_sec );
    } else if (strcmpIgnoreCase(format, TEXT("YYYYMMDDHHII")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d%02d%02d%02d"),
        timeTM->tm_year + 1900, timeTM->tm_mon + 1, timeTM->tm_mday,
        timeTM->tm_hour, timeTM->tm_min );
    } else if (strcmpIgnoreCase(format, TEXT("YYYYMMDDHH")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d%02d%02d"),
        timeTM->tm_year + 1900, timeTM->tm_mon + 1, timeTM->tm_mday,
        timeTM->tm_hour );
    } else if (strcmpIgnoreCase(format, TEXT("YYYYMMDD")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d%02d"),
        timeTM->tm_year + 1900, timeTM->tm_mon + 1, timeTM->tm_mday);
    } else {
        _sntprintf(generateValueBuffer, 256, TEXT("{INVALID}"));
    }
    return generateValueBuffer;
}

/**
 * This function returns a reference to a static buffer and is NOT thread safe.
 *  It is currently called only when loading a property file and when firing an event.
 *  Both happen in the main thread.
 * The largest return value can be 9+1 characters.
 */
TCHAR* generateRandValue(const TCHAR* format) {
    if (strcmpIgnoreCase(format, TEXT("N")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%01d"), rand() % 10);
    } else if (strcmpIgnoreCase(format, TEXT("NN")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%02d"), rand() % 100);
    } else if (strcmpIgnoreCase(format, TEXT("NNN")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%03d"), rand() % 1000);
    } else if (strcmpIgnoreCase(format, TEXT("NNNN")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d"), rand() % 10000);
    } else if (strcmpIgnoreCase(format, TEXT("NNNNN")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%01d"), rand() % 10000, rand() % 10);
    } else if (strcmpIgnoreCase(format, TEXT("NNNNNN")) == 0) {
        _sntprintf(generateValueBuffer, 256, TEXT("%04d%02d"), rand() % 10000, rand() % 100);
    } else {
        _sntprintf(generateValueBuffer, 256, TEXT("{INVALID}"));
    }
    return generateValueBuffer;
}

/**
 * Parses a property value and populates any environment variables.  If the expanded
 *  environment variable would result in a string that is longer than bufferLength
 *  the value is truncated.
 *
 * @param propertyValue The property value to be parsed.
 * @param buffer output buffer where the expanded string will be copied.
 * @param bufferLength size of the buffer.
 * @param warnUndefinedVars Log warnings about missing environment variables.
 * @param warnedUndefVarMap Map of variables which have previously been logged, may be NULL if warnUndefinedVars false.
 * @param warnLogLevel Log level at which any warnings will be logged.
 * @param ignoreVarMap Map of environment variables that should not be expanded.
 * @param pHasPercentage Pointer to a variable which will be set to TRUE if a %WRAPPER_PERCENTAGE% variable was found.
 *                         - If a non-NULL pointer is passed, the variable will not be expanded and no warning will be reported.
 *                         - If NULL is passed, the variable will be expanded to '%'.
 */
void evaluateEnvironmentVariables(const TCHAR *propertyValue, TCHAR *buffer, int bufferLength, int warnUndefinedVars, PHashMap warnedUndefVarMap, int warnLogLevel, PHashMap ignoreVarMap, int *pHasPercentage) {
    const TCHAR *in;
    TCHAR *out;
    TCHAR *envName;
    TCHAR *envValue;
    int envValueNeedFree;
    TCHAR *start;
    TCHAR *end;
    size_t len;
    size_t outLen;
    size_t bufferAvailable;
    const TCHAR* ignore;

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("evaluateEnvironmentVariables(properties, '%s', buffer, %d, %d)"), propertyValue, bufferLength, warnUndefinedVars);
#endif

    buffer[0] = TEXT('\0');
    in = propertyValue;
    out = buffer;
    bufferAvailable = bufferLength - 1; /* Reserver room for the null terminator */

    /* Loop until we hit the end of string. */
    while (in[0] != TEXT('\0')) {
#ifdef _DEBUG
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("    initial='%s', buffer='%s'"), propertyValue, buffer);
#endif

        start = _tcschr(in, TEXT('%'));
        if (start != NULL) {
            end = _tcschr(start + 1, TEXT('%'));
            if (end != NULL) {
                /* A pair of '%' characters was found.  An environment */
                /*  variable name should be between the two. */
                len = (int)(end - start - 1);
                envName = malloc(sizeof(TCHAR) * (len + 1));
                if (envName == NULL) {
                    outOfMemory(TEXT("EEV"), 1);
                    return;
                }
                _tcsncpy(envName, start + 1, len);
                envName[len] = TEXT('\0');
                
                /* See if it is a special dynamic environment variable */
                envValueNeedFree = FALSE;
                if (_tcsstr(envName, TEXT("WRAPPER_TIME_")) == envName) {
                    /* Found a time value. */
                    envValue = generateTimeValue(envName + 13, &loadPropertiesTM);
                } else if (_tcsstr(envName, TEXT("WRAPPER_RAND_")) == envName) {
                    /* Found a time value. */
                    envValue = generateRandValue(envName + 13);
                } else if (_tcsstr(envName, TEXT("WRAPPER_PERCENTAGE")) == envName) {
                    envValue = NULL;
                    if (pHasPercentage) {
                        /* Do not warn about this variable. Do not expand it. We will expand it later. */
                        hashMapPutKWVW(warnedUndefVarMap, envName, envName);
                        *pHasPercentage = TRUE;
                    } else {
                        envValue = TEXT("%");
                    }
                } else {
                    if (ignoreVarMap) {
                        /* Can return NULL if missing or "TRUE" or "FALSE". */
                        ignore = hashMapGetKWVW(ignoreVarMap, envName);
                    } else {
                        ignore = NULL;
                    }
                    if (!ignore || strcmpIgnoreCase(ignore, TEXT("TRUE")) != 0) {
                        /* Try looking up the environment variable. */
                        envValue = _tgetenv(envName);
#if !defined(WIN32) && defined(UNICODE)
                        envValueNeedFree = TRUE;
#endif
                    } else {
                        envValue = NULL;
                        envValueNeedFree = FALSE;
                    }
                }

                if (envValue != NULL) {
                    /* An envvar value was found. */
                    /* Copy over any text before the envvar */
                    outLen = (int)(start - in);
                    if (bufferAvailable < outLen) {
                        outLen = bufferAvailable;
                    }
                    if (outLen > 0) {
                        _tcsncpy(out, in, outLen);
                        out += outLen;
                        bufferAvailable -= outLen;
                    }

                    /* Copy over the env value */
                    outLen = _tcslen(envValue);
                    if (bufferAvailable < outLen) {
                        outLen = bufferAvailable;
                    }
                    if (outLen > 0) {
                        _tcsncpy(out, envValue, outLen);
                        out += outLen;
                        bufferAvailable -= outLen;
                    }
                    
                    if (envValueNeedFree) {
                        free(envValue);
                    }

                    /* Terminate the string */
                    out[0] = TEXT('\0');

                    /* Set the new in pointer */
                    in = end + 1;
                } else {
                    /* Not found.  So copy over the input up until the */
                    /*  second '%'.  Leave it in case it is actually the */
                    /*  start of an environment variable name */
                    outLen = len = end - in + 1;
                    if (bufferAvailable < outLen) {
                        outLen = bufferAvailable;
                    }
                    if (outLen > 0) {
                        _tcsncpy(out, in, outLen);
                        out += outLen;
                        bufferAvailable -= outLen;
                    }
                    in += len;

                    /* Terminate the string */
                    out[0] = TEXT('\0');
                    
                    if (warnUndefinedVars) {
                        if (hashMapGetKWVW(warnedUndefVarMap, envName) == NULL) {
                            /* This is the first time this environment variable was noticed, so report it to the user then remember so we don't report it again. */
                            log_printf(WRAPPER_SOURCE_WRAPPER, warnLogLevel, TEXT("The '%s' environment variable was referenced but has not been defined."), envName);
                            hashMapPutKWVW(warnedUndefVarMap, envName, envName);
                        }
                    }
                }
                
                free(envName);
            } else {
                /* Only a single '%' TCHAR was found. Leave it as is. */
                outLen = len = _tcslen(in);
                if (bufferAvailable < outLen) {
                    outLen = bufferAvailable;
                }
                if (outLen > 0) {
                    _tcsncpy(out, in, outLen);
                    out += outLen;
                    bufferAvailable -= outLen;
                }
                in += len;

                /* Terminate the string */
                out[0] = TEXT('\0');
            }
        } else {
            /* No more '%' chars in the string. Copy over the rest. */
            outLen = len = _tcslen(in);
            if (bufferAvailable < outLen) {
                outLen = bufferAvailable;
            }
            if (outLen > 0) {
             _tcsncpy(out, in, outLen);
              out += outLen;
                bufferAvailable -= outLen;
            }
            in += len;

            /* Terminate the string */
            out[0] = TEXT('\0');
        }
    }
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  final buffer='%s'"), buffer);
#endif
}

/**
 *
 * @param warnUndefinedVars Log warnings about missing environment variables.
 */
void setInnerProperty(Properties *properties, Property *property, const TCHAR *propertyValue, int warnUndefinedVars) {
    int i, count;
    /* The property value is expanded into a large buffer once, but that is temporary.  The actual
     *  value is stored in the minimum required size. */
    TCHAR *buffer;
    int hasPercentage = FALSE;
    
    /* Free any existing value */
    if (property->value != NULL) {
        free(property->value);
        property->value = NULL;
    }
    if (property->value_inner != NULL) {
        free(property->value_inner);
        property->value_inner = NULL;
    }

    /* Set the new value using a copy of the provided value. */
    if (propertyValue != NULL) {
        buffer = malloc(MAX_PROPERTY_VALUE_LENGTH * sizeof(TCHAR));
        if (buffer) {
                evaluateEnvironmentVariables(propertyValue, buffer, MAX_PROPERTY_VALUE_LENGTH, warnUndefinedVars, properties->warnedVarMap, properties->logWarningLogLevel, properties->ignoreVarMap, &hasPercentage);

            /* We need to keep a copy of the value without expanding %WRAPPER_PERCENTAGE% (see the function description of evaluateEnvironmentVariables()) */
            property->value_inner = malloc(sizeof(TCHAR) * (_tcslen(buffer) + 1));
            if (!property->value_inner) {
                outOfMemoryQueued(TEXT("SIP"), 1);
            } else {
                /* Strip any non valid characters like control characters. Some valid characters are
                 *  less than 0 when the TCHAR is unsigned. */
                for (i = 0, count = 0; i < (int)_tcslen(buffer); i++) {
                    /* Only add valid characters, skipping any control characters EXCEPT for a line feed. */
                    if ((buffer[i] == TEXT('\n')) || (!_istcntrl(buffer[i]))) {
                        property->value_inner[count++] = buffer[i];
                    }
                }

                /* Crop string to new size */
                property->value_inner[count] = TEXT('\0');

                if (hasPercentage) {
                    /* Now expand the percentages */
                    evaluateEnvironmentVariables(property->value_inner, buffer, MAX_PROPERTY_VALUE_LENGTH, warnUndefinedVars, properties->warnedVarMap, properties->logWarningLogLevel, properties->ignoreVarMap, NULL);
                    property->value = malloc(sizeof(TCHAR) * (_tcslen(buffer) + 1));
                    if (!property->value) {
                        outOfMemoryQueued(TEXT("SIP"), 2);
                    } else {
                        _tcsncpy(property->value, buffer, _tcslen(buffer) + 1);
                    }
                }
                if (!property->value) {
                    property->value = malloc(sizeof(TCHAR) * (_tcslen(property->value_inner) + 1));
                    if (!property->value) {
                        outOfMemoryQueued(TEXT("SIP"), 3);
                    } else {
                        _tcsncpy(property->value, property->value_inner, _tcslen(property->value_inner) + 1);
                    }
                }
            }
            free(buffer); 
        } else {
            outOfMemoryQueued(TEXT("SIP"), 4);
        }
    }
}

/**
 * Check if the given buffer matches the syntax of a property (to be used before actually creating the property).
 * The buffer should contain a '=' and the name on its left should not contain any space once it has been trimmed.
 * A null termination character will be inserted before the first '=' in the line.
 *
 * @param buffer The full line to be checked.
 *
 * @return a pointer to the string representation of the value (i.e the part of the buffer after the '=')
 *         or NULL if the line did not match the syntax of a property.
 */
static TCHAR* checkPropertySyntax(TCHAR* buffer) {
    TCHAR *keyTrim;
    TCHAR *d;

    /* The buffer should contain a '='. */
    if ((d = _tcschr(buffer, TEXT('='))) != NULL) {
        *d = TEXT('\0');
        d++;
        
        keyTrim = malloc(sizeof(TCHAR) * (_tcslen(buffer) + 1));
        if (!keyTrim) {
            outOfMemory(TEXT("CPS"), 1);
            return NULL;
        }
        trim(buffer, keyTrim);
        
        /* The trimmed key should not contain any space. */
        if (_tcschr(keyTrim, TEXT(' ')) == NULL) {
            free(keyTrim);
            return d;
        }
        free(keyTrim);
    }
    
    return NULL;
}

static int loadPropertiesCallback(void *callbackParam, const TCHAR *fileName, int lineNumber, int depth, TCHAR *config, int exitOnOverwrite, int logLevelOnOverwrite) {
    Properties *properties = (Properties *)callbackParam;
    TCHAR *d;

    properties->exitOnOverwrite = exitOnOverwrite;
    properties->logLevelOnOverwrite = logLevelOnOverwrite;

    /* special case where the callback should only update the properties structure */
    if ((fileName == NULL) && (lineNumber == -1) && (config == NULL)) {
        return TRUE;
    }
    
    if (_tcsstr(config, TEXT("include")) == config) {
        /* Users sometimes remove the '#' from include statements.
           Add a warning to help them notice the problem. */
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_ADVICE,
                   TEXT("Include file reference missing leading '#': %s"), config);
    } else if ((d = checkPropertySyntax(config))) {
        addProperty(properties, fileName, lineNumber, depth, config, d, FALSE, FALSE, TRUE, FALSE);
    }

    return TRUE;
}

/**
 * Create a Properties structure loaded in from the specified file.
 *  Must call disposeProperties to free up allocated memory.
 *
 * @param properties Properties structure to load into.
 * @param filename File to load the properties from.
 * @param preload TRUE if this is a preload call that should have supressed error output.
 * @param originalWorkingDir Working directory of the binary at the moment it was launched.
 * @param fileRequired TRUE if the file specified by filename is required, FALSE if a missing
 *                     file will silently fail.
 * @param readFilterCallback Pointer to a callback funtion which will be used to filter some
 *                           lines that should not be processed.
 *
 * @return CONFIG_FILE_READER_SUCCESS if the file was read successfully,
 *         CONFIG_FILE_READER_OPEN_FAIL if the file could not be found or opened.
 *         CONFIG_FILE_READER_FAIL if there were any problems at all, or
 *         CONFIG_FILE_READER_HARD_FAIL if the problem should cascaded all the way up.
 */
int loadProperties(Properties *properties,
                   const TCHAR* filename,
                   int preload,
                   const TCHAR *originalWorkingDir,
                   int fileRequired,
                   ConfigFileReader_ReadFilterCallbackMB readFilterCallback) {
    /* Store the time that the property file began to be loaded. */
    #ifdef WIN32
    struct _timeb timebNow;
    #else
    struct timeval timevalNow;
    #endif
    time_t now;
    struct tm *nowTM;
    
#ifdef WIN32
    _ftime(&timebNow);
    now = (time_t)timebNow.time;
#else
    gettimeofday(&timevalNow, NULL);
    now = (time_t)timevalNow.tv_sec;
#endif
    nowTM = localtime(&now);
    memcpy(&loadPropertiesTM, nowTM, sizeof(struct tm));
    
    return configFileReader(filename, fileRequired, loadPropertiesCallback, properties, readFilterCallback, TRUE, preload, originalWorkingDir, properties->warnedVarMap, properties->ignoreVarMap, properties->logWarnings, properties->logWarningLogLevel);
}

/**
 * Get the log level of the messages reported when properties are overwritten.
 *
 * @param properties 
 *
 * @return log level, or -1 if AUTO
 */
int GetLogLevelOnOverwrite(Properties *properties) {
    /* Should be at least LEVEL_FATAL if exitOnOverwrite is set to TRUE */
    if (properties) {
        if (properties->exitOnOverwrite) {
            return __max(properties->logLevelOnOverwrite, LEVEL_FATAL);
        }
        return properties->logLevelOnOverwrite;
    }
    return LEVEL_UNKNOWN;
}

Properties* createProperties(int debug, int logLevelOnOverwrite, int exitOnOverwrite) {
    Properties *properties = malloc(sizeof(Properties));
    if (!properties) {
        outOfMemory(TEXT("CP"), 1);
        return NULL;
    }
    properties->debugProperties = debug;
    properties->exitOnOverwrite = exitOnOverwrite;
    properties->logLevelOnOverwrite = logLevelOnOverwrite;
    properties->overwrittenPropertyCausedExit = FALSE;
    properties->logWarnings = TRUE;
    properties->logWarningLogLevel = LEVEL_WARN;
    properties->first = NULL;
    properties->last = NULL;
    properties->warnedVarMap = newHashMap(8);
    properties->ignoreVarMap = newHashMap(8);
    properties->dumpFormat = NULL;
    if ((!properties->warnedVarMap) || (!properties->ignoreVarMap)) {
        disposeProperties(properties);
        return NULL;
    }
    return properties;
}

void disposeProperties(Properties *properties) {
    /* Loop and dispose any Property structures */
    Property *tempProperty;
    Property *property;
    
    if (properties) {
        property = properties->first;
        properties->first = NULL;
        properties->last = NULL;
        while (property != NULL) {
            /* Save the next property */
            tempProperty = property->next;
    
            /* Clean up the current property */
            disposeInnerProperty(property);
            property = NULL;
    
            /* set the current property to the next. */
            property = tempProperty;
        }
        
        if (properties->dumpFormat) {
            free(properties->dumpFormat);
        }
        
        if (properties->warnedVarMap) {
            freeHashMap(properties->warnedVarMap);
        }
        
        if (properties->ignoreVarMap) {
            freeHashMap(properties->ignoreVarMap);
        }
        
        /* Dispose the Properties structure */
        free(properties);
        properties = NULL;
    }
}

/**
 * This method cleans the environment at shutdown.
 */
void disposeEnvironment() {
    EnvSrc *current, *previous;

    if (baseEnvSrc) {
        current = baseEnvSrc;
        while (current != NULL) {
            free(current->name);
            previous = current;
            current = current->next;
            free(previous);
        }
        baseEnvSrc = NULL;
    }
}

void disconnectProperty(Properties *properties, Property *property) {
    Property *next;
    Property *previous;

    next = property->next;
    previous = property->previous;

    if (next == NULL) {
        /* This was the last property */
        properties->last = previous;
    } else {
        next->previous = property->previous;
    }
    if (previous ==  NULL) {
        /* This was the first property */
        properties->first = next;
    } else {
        previous->next = property->next;
    }
}

/**
 * Remove a single Property from a Properties.  All associated memory is
 *  freed up.
 *
 * @return TRUE if the property was found, FALSE otherwise.
 */
int removeProperty(Properties *properties, const TCHAR *propertyName) {
    Property *property;

    /* Look up the property */
    property = getInnerProperty(properties, propertyName, FALSE);
    if (property == NULL) {
        /* The property did not exist, so nothing to do. */
    } else {
        /* Disconnect the property */
        disconnectProperty(properties, property);

        /* Now that property is disconnected, if can be disposed. */
        disposeInnerProperty(property);
        return TRUE;
    }
    return FALSE;
}

/**
 * Sets an environment variable with the specified value.
 *  The function will only set the variable if its value is changed, but if
 *  it does, the call will result in a memory leak the size of the string:
 *   "name=value".
 *
 * For Windows, the putenv_s funcion looks better, but it is not available
 *  on some older SDKs and non-pro versions of XP.
 *
 * @param name Name of the variable being set.
 * @param value Value to be set, NULL to clear it.
 *
 * Return TRUE if there were any problems, FALSE otherwise.
 */
int setEnvInner(const TCHAR *name, const TCHAR *value) {
    int result = FALSE;
    TCHAR *oldVal;
#ifdef WIN32
 #if !defined(WRAPPER_USE_PUTENV_S)
    size_t len;
    TCHAR *envBuf;
 #endif
#endif
#if defined(WRAPPER_USE_PUTENV)
    size_t len;
    TCHAR *envBuf;
#endif

    /* Get the current environment variable value so we can avoid allocating and
     *  setting the variable if it has not changed its value. */
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("setEnvInner '%s=%s'."), name, value);
#endif
    oldVal = _tgetenv(name);
    if (value == NULL) {
        /*_tprintf("clear %s=\n", name);*/
        /* Only clear the variable if it is actually set to avoid unnecessary leaks. */
        if (oldVal != NULL) {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("setEnvInner (CLEAR) '%s=%s'."), name, value);
#endif
#ifdef WIN32
 #if defined(WRAPPER_USE_PUTENV_S)
            if (_tputenv_s(name, TEXT("")) == EINVAL) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to clear the '%s' environment variable."), name);
                result = TRUE;
            }
 #else
            len = _tcslen(name) + 1 + 1;
            envBuf = malloc(sizeof(TCHAR) * len);
            if (!envBuf) {
                outOfMemory(TEXT("SEI"), 1);
                result = TRUE;
            } else {
                _sntprintf(envBuf, len, TEXT("%s="), name);
                /* The memory pointed to by envBuf should only be freed if this is UNICODE. */
                if (_tputenv(envBuf)) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to clear the '%s' environment variable."), name);
                    result = TRUE;
                }
            }
 #endif
#else
 #if defined(WRAPPER_USE_PUTENV)
            len = _tcslen(name) + 1 + 1;
            envBuf = malloc(sizeof(TCHAR) * len);
            if (!envBuf) {
                outOfMemory(TEXT("SEI"), 1);
                result = TRUE;
            } else {
                _sntprintf(envBuf, len, TEXT("%s="), name);
                /* The memory pointed to by envBuf should only be freed if this is UNICODE. */
                if (_tputenv(envBuf)) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to clear the '%s' environment variable."), name);
                    result = TRUE;
                }
  #ifdef UNICODE
                free(envBuf);
  #endif
            }
 #else
            _tunsetenv(name);
 #endif
#endif
        }
    } else {
        /*_tprintf("set %s=%s\n", name, value);*/
        if ((oldVal == NULL) || (_tcscmp(oldVal, value) != 0)) {
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("setEnvInner (SET) '%s=%s'."), name, value);
#endif
#ifdef WIN32
 #if defined(WRAPPER_USE_PUTENV_S)
            if (_tputenv_s(name, value) == EINVAL) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to set the '%s' environment variable to: %s"), name, value);
                result = TRUE;
            }
 #else
            len = _tcslen(name) + 1 + _tcslen(value) + 1;
            if (len > MAX_ENV_PAIR_LEN) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to set the '%s' environment variable because total pair length of %d is longer than maximum for the OS of %d."), name, len,  MAX_ENV_PAIR_LEN);
                result = TRUE;
            } else {
                envBuf = malloc(sizeof(TCHAR) * len);
                if (!envBuf) {
                    outOfMemory(TEXT("SEI"), 2);
                    result = TRUE;
                } else {
                    _sntprintf(envBuf, len, TEXT("%s=%s"), name, value);
                    /* The memory pointed to by envBuf should only be freed if this is UNICODE. */
                    if (_tputenv(envBuf)) {
                        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to set environment variable: %s=%s"), name, value);
                        result = TRUE;
                    }
                }
            }
 #endif
#else
 #if defined(WRAPPER_USE_PUTENV)
            len = _tcslen(name) + 1 + _tcslen(value) + 1;
            envBuf = malloc(sizeof(TCHAR) * len);
            if (!envBuf) {
                outOfMemory(TEXT("SEI"), 2);
                result = TRUE;
            } else {
                _sntprintf(envBuf, len, TEXT("%s=%s"), name, value);
                /* The memory pointed to by envBuf should only be freed if this is UNICODE. */
                if (_tputenv(envBuf)) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to set the '%s' environment variable to: %s"), name, value);
                    result = TRUE;
                }
  #ifdef UNICODE
                free(envBuf);
  #endif
            }
 #else
            if (_tsetenv(name, value, TRUE)) {
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL, TEXT("Unable to set the '%s' environment variable to: %s"), name, value);
                result = TRUE;
            }
 #endif
#endif
        }
    }

#if !defined(WIN32) && defined(UNICODE)
    if (oldVal != NULL) {
        free(oldVal);
    }
#endif

    return result;
}

/**
 * Sets an environment variable with the specified value.
 *  The function will only set the variable if its value is changed, but if
 *  it does, the call will result in a memory leak the size of the string:
 *   "name=value".
 *
 * @param name Name of the variable being set.
 * @param value Value to be set, NULL to clear it.
 * @param source Where the variable came from.
 *               Must be one of ENV_SOURCE_PARENT, ENV_SOURCE_APPLICATION, ENV_SOURCE_CONFIG,
 *                 or ENV_SOURCE_REG_SYSTEM or ENV_SOURCE_REG_ACCOUNT on Windows.
 *               If value is ENV_SOURCE_PARENT then the value may be NULL and will never be
 *                 set to the environment.
 *
 * Return TRUE if there were any problems, FALSE otherwise.
 */
int setEnv(const TCHAR *name, const TCHAR *value, int source) {
    EnvSrc **thisEnvSrcRef;
    EnvSrc *thisEnvSrc;
    size_t len;
    TCHAR *nameCopy;
    EnvSrc *newEnvSrc;
    int cmpRes;
    
#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("setEnv(%s, %s, %d)"), name, value, source);
#endif
    thisEnvSrcRef = &baseEnvSrc;
    thisEnvSrc = baseEnvSrc;
    
    /* Create a copy of the name so we can store it. */
    len = _tcslen(name) + 1;
    nameCopy = malloc(sizeof(TCHAR) * len);
    if (!nameCopy) {
        outOfMemory(TEXT("SE"), 1);
        return TRUE;
    }
    _sntprintf(nameCopy, len, TEXT("%s"), name);
    
    /* Figure out where we want to set the value. */
    while (thisEnvSrc) {
        cmpRes = strcmpIgnoreCase(thisEnvSrc->name, name);
        if (cmpRes == 0) {
            /* This is the same value.  It is being changed. */
            /* The nameCopy is not needed so free it up. */
            free(nameCopy);
            thisEnvSrc->source |= source;
            if (source != ENV_SOURCE_PARENT) {
                return setEnvInner(name, value);
            }
            return FALSE;
        } else if (cmpRes > 0) {
            /* This EnvSrc would be after the one being set, so we need to insert it. */
            newEnvSrc = malloc(sizeof(EnvSrc));
            if (!newEnvSrc) {
                outOfMemory(TEXT("SEV"), 2);
                return TRUE;
            }
            newEnvSrc->source = source;
            newEnvSrc->name = nameCopy;
            newEnvSrc->next = thisEnvSrc;
            *thisEnvSrcRef = newEnvSrc;
            if (source != ENV_SOURCE_PARENT) {
                return setEnvInner(name, value);
            }
            return FALSE;
        } else {
            /* This EnvSrc would be before the one being set, so keep looking. */
            thisEnvSrcRef = &(thisEnvSrc->next);
            thisEnvSrc = thisEnvSrc->next;
        }
    }
    
    /* If we get here then we are at the end of the list. */
    thisEnvSrc = malloc(sizeof(EnvSrc));
    if (!thisEnvSrc) {
        outOfMemory(TEXT("SEV"), 3);
        return TRUE;
    }
    thisEnvSrc->source = source;
    thisEnvSrc->name = nameCopy;
    thisEnvSrc->next = NULL;
    *thisEnvSrcRef = thisEnvSrc;
    if (source != ENV_SOURCE_PARENT) {
        return setEnvInner(name, value);
    }
    return FALSE;
}

/**
 * Used to set a NULL terminated list of property names whose values should be
 *  escaped when read in from a file.   '\\' will become '\' and '\n' will
 *  become '^J', all other characters following '\' will be left as is.
 *
 * @param propertyNames NULL terminated list of property names.  Property names
 *                      can contain a single '*' wildcard which will match 0 or
 *                      more characters.
 */
void setEscapedProperties(const TCHAR **propertyNames) {
    escapedPropertyNames = propertyNames;
}

/**
 * Returns true if the specified property matches one of the property names
 *  previously set in a call to setEscapedProperties()
 *
 * @param propertyName Property name to test.
 *
 * @return TRUE if the property should be escaped.  FALSE otherwise.
 */
int isEscapedProperty(const TCHAR *propertyName) {
    size_t nameLen;
    size_t i;
    const TCHAR *pattern;
    TCHAR *wildPos;
    size_t headLen;
    size_t tailLen;
    int matched;
    size_t patternI;
    size_t nameI;

    if (escapedPropertyNames) {
        nameLen = _tcslen(propertyName);
        i = 0;
        while (escapedPropertyNames[i]) {
            pattern = escapedPropertyNames[i];
            if (strcmpIgnoreCase(pattern, propertyName) == 0) {
                /* Direct Match. */
#ifdef _DEBUG
                _tprintf(TEXT("Property %s matched pattern %s\n"), propertyName, pattern);
#endif
                return TRUE;
            } else {
                wildPos = _tcschr(pattern, TEXT('*'));
                if (wildPos) {
                    /* The string contains a wildcard. */

                    /* Try to match the head of the property name. */
                    headLen = wildPos - pattern;
                    if (headLen < nameLen) {
                        matched = TRUE;
                        patternI = 0;
                        nameI = 0;
                        while (patternI < headLen) {
                            if (pattern[patternI] != propertyName[nameI]) {
                                matched = FALSE;
                                break;
                            }
                            patternI++;
                            nameI++;
                        }

                        if (matched) {
                            tailLen = _tcslen(pattern) - headLen - 1;
                            if (tailLen < nameLen - headLen) {
                                matched = TRUE;
                                patternI = headLen + 1;
                                nameI = nameLen - tailLen;
                                while (nameI < nameLen) {
                                    if (pattern[patternI] != propertyName[nameI]) {
                                        matched = FALSE;
                                        break;
                                    }
                                    patternI++;
                                    nameI++;
                                }
                                if (matched) {
#ifdef _DEBUG
                                    _tprintf(TEXT("Property %s matched pattern %s\n"), propertyName, pattern);
#endif
                                    return TRUE;
                                }
                            }
                        }
                    }
                }
            }

            i++;
        }
    }

    return FALSE;
}

/**
 * Expands escaped characters and returns a newly malloced string with the result.
 *  '\n' replaced with '^J'
 *  '\\' replaced with '\'
 *  Other escaped characters will show as is.
 *
 * @param buffer Original buffer containing escaped characters.
 *
 * @return The new expanded buffer.  It is the responsibility of the caller to free memory later.
 */
TCHAR *expandEscapedCharacters(const TCHAR* buffer) {
    size_t inPos;
    size_t outPos;
    TCHAR *outBuffer;
    int i;
    TCHAR c1, c2;

    /* First count the length of the required output buffer to hold the current line. Use the same code twice to avoid maintenance problems.  */
    outBuffer = NULL;
    for (i = 0; i < 2; i++) {
        inPos = 0;
        outPos = 0;
        do {
            c1 = buffer[inPos];
            /* The real backslash is #92.  The yen mark from files loaded from ShiftJIS is #165. */
            if ((c1 == TEXT('\\')) || (c1 == 165)) {
                /* Escape. */
                c2 = buffer[inPos + 1];
                if (c2 == TEXT('n')) {
                    /* Line feed. */
                    inPos++;
                    if (outBuffer) {
                        outBuffer[outPos] = TEXT('\n');
                    }
                    outPos++;
                } else if ((c2 == TEXT('\\')) || (c2 == 165)) {
                    /* Back slash. */
                    inPos++;

                    if (outBuffer) {
                        outBuffer[outPos] = c1;
                    }
                    outPos++;
                } else if (c2 == 0) {
                    /* Premature End of buffer.  Show the backslash. */
                    if (outBuffer) {
                        outBuffer[outPos] = c1;
                    }
                    outPos++;
                    c1 = 0;
                } else {
                    /* Unknown char, show the unescaped backslash. */
                    inPos++;

                    if (outBuffer) {
                        outBuffer[outPos] = c1;
                        outBuffer[outPos + 1] = c2;
                    }
                    outPos += 2;
                }
                inPos++;
            } else if (c1 == 0) {
                /* End of buffer. */
            } else {
                /* Normal character. */
                if (outBuffer) {
                    outBuffer[outPos] = c1;
                }
                outPos++;
                inPos++;
            }
        } while (c1 != 0);

        /* string terminator. */
        if (outBuffer) {
            outBuffer[outPos] = TEXT('\0');
        }
        outPos++;

        if (outBuffer) {
            /* We have have full outBuffer. Fall through. */
        } else {
            /* First pass. We need to allocate the outBuffer. */
            outBuffer = malloc(outPos * sizeof(TCHAR));
            if (!outBuffer) {
                outOfMemory(TEXT("ELF"), 1);
                return NULL;
            }
        }
    }
    return outBuffer;
}

/**
 * Return TRUE if the value of the property should be displayed as '<hidden>' when printed in the logs.
 *
 * @property property to check.
 *
 * @return TRUE if the value should be hidden.
 */
int isSecretValue(Property *property) {
    TCHAR* propName;
    int result = TRUE;
    
    propName = toLower(property->name);
    if (propName) {
        result = (_tcsstr(propName, TEXT(".password")) == (propName + ((int)_tcslen(propName) - 9)));
        free(propName);
    }
    
    return result;
}

/**
 * Allocate a string which is used to display the value of a property in the logs.
 *
 * @value  property value.
 * @hidden whether the value should be hidden or not.
 *
 * @return the allocated value. Should be freed by the caller.
 */
TCHAR* getDisplayValue(const TCHAR *value, int hidden) {
    int i, j;
    TCHAR* buffer;
    size_t len;
    
    /* We need to malloc the buffer! Before, the buffer was received as an argument, but its big
     *  size caused the stack to overflow on certain platforms (I experienced this on HPUX-IA).
     *  I could have used static variables to force creating the buffers on the heap, but they
     *  would remain in memory all the time the Wrapper is running. */
    if (hidden) {
        buffer = malloc(sizeof(TCHAR) * 9);
        if (!buffer) {
            outOfMemory(TEXT("GDV"), 1);
            return NULL;
        }
        _tcsncpy(buffer, TEXT("<hidden>"), 9);
    } else {
        /* Count all newlines if any. */
        for(i = 0, j = 0; i < (int)_tcslen(value); i++) {
            if ((value)[i] == TEXT('\n')) {
                j++;
            }
        }
        
        len = _tcslen(value) + j + 1;
        buffer = malloc(sizeof(TCHAR) * len);
        if (!buffer) {
            outOfMemory(TEXT("GDV"), 2);
            return NULL;
        }
        
        if (j > 0) {
            /* Replace all newlines with '\n'. */
            for(i = 0, j = 0; i < (int)_tcslen(value) + 1; i++) {
                if ((value)[i] == TEXT('\n')) {
                    buffer[j++] = TEXT('\\');
                    buffer[j++] = TEXT('n');
                } else {
                    buffer[j++] = value[i];
                }
            }
        } else {
            _tcsncpy(buffer, value, len);
        }
    }
    return buffer;
}

/**
 * Adds a single property to the properties structure.
 *
 * @param properties Properties structure to add to.
 * @param filename Name of the file from which the property was loaded.  NULL, if not from a file.
 * @param lineNum Line number of the property declaration in the file.  Ignored if filename is NULL.
 * @param depth Depth of the configuration file where the property was declared.  Ignored if filename is NULL.
 * @param propertyName Name of the new Property.
 * @param propertyValue Initial property value.
 * @param finalValue TRUE if the property should be set as static.
 * @param quotable TRUE if the property could contain quotes.
 * @param escapable TRUE if the propertyValue can be escaped if its propertyName
 *                  is in the list set with setEscapedProperties().
 * @param internal TRUE if the property is a Wrapper internal property.
 *
 * @return The newly created Property, or NULL if there was a reported error.
 */
Property* addProperty(Properties *properties, const TCHAR* filename, int lineNum, int depth, const TCHAR *propertyName, const TCHAR *propertyValue, int finalValue, int quotable, int escapable, int internal) {
    int setValue;
    Property *property;
    TCHAR *oldVal;
    TCHAR *propertyNameTrim;
    const TCHAR *propertyValueNotNull;
    TCHAR *propertyValueTrim;
    TCHAR *propertyExpandedValue;
    int logLevelOnOverwrite;
    int hidden;
    TCHAR *dispValue1;
    TCHAR *dispValue2;
    int overwriteWarnId;
    int isShownAsInternal;
    
    propertyValueNotNull = propertyValue ? propertyValue : TEXT("<NULL>");

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("addProperty(properties, %s, '%s', '%s', %d, %d, %d, %d)"),
        (filename ? filename : TEXT("<NULL>")), propertyName, propertyValue, finalValue, quotable, escapable, internal);
#endif
    /* It is possible that the propertyName and or properyValue contains extra spaces. */
    propertyNameTrim = malloc(sizeof(TCHAR) * (_tcslen(propertyName) + 1));
    if (!propertyNameTrim) {
        outOfMemory(TEXT("AP"), 1);
        return NULL;
    }
    trim(propertyName, propertyNameTrim);
    propertyValueTrim = malloc(sizeof(TCHAR) * ( _tcslen(propertyValueNotNull) + 1));
    if (!propertyValueTrim) {
        outOfMemory(TEXT("AP"), 2);
        free(propertyNameTrim);
        return NULL;
    }
    trim(propertyValueNotNull, propertyValueTrim);

#ifdef _DEBUG
    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  trimmed name='%s', value='%s'"),
        propertyNameTrim, propertyValueTrim);
#endif

    /* See if the property already exists */
    setValue = TRUE;
    property = getInnerProperty(properties, propertyNameTrim, FALSE);
    if (property == NULL) {
        /* This is a new property */
        property = createInnerProperty();
        if (!property) {
            free(propertyNameTrim);
            free(propertyValueTrim);
            return NULL;
        }

        /* Store a copy of the name */
        property->name = malloc(sizeof(TCHAR) * (_tcslen(propertyNameTrim) + 1));
        if (!property->name) {
            outOfMemory(TEXT("AP"), 3);
            disposeInnerProperty(property);
            free(propertyNameTrim);
            free(propertyValueTrim);
            return NULL;
        }
        _tcsncpy(property->name, propertyNameTrim, _tcslen(propertyNameTrim) + 1);

        /* Insert this property at the correct location.  Value will still be null. */
            insertInnerProperty(properties, property);
    } else {
        /* The property was already set.  Only change it if non final and non internal */
        if (property->finalValue || (property->internal && !internal)) {
            setValue = FALSE;
        }
        property->definitions++;
        
        /* On preload we set properties->debugProperties to false as we don't want to log anything nor to stop the Wrapper at this stage. */  
        if (properties->debugProperties) {
            /* Preload was already done so the logging system is ready. */
            logLevelOnOverwrite = GetLogLevelOnOverwrite(properties);
            
            isShownAsInternal = property->internal;
            if (logLevelOnOverwrite == -1) {
                /* Log level on overwrite is AUTO. */
                if (((property->lastDefinitionDepth >= depth) && (!property->finalValue)) ||    /* if the new property is referenced in a file with a lower inclusion depth and not overriding a command property. */
                    (finalValue && (!setValue)) ||                                              /* if the new property is a command property that can't be set. */
                    (isShownAsInternal && (!internal))) {                                       /* if there is any attempt to override an internal property. */
                    logLevelOnOverwrite = LEVEL_WARN;
                } else {
                    logLevelOnOverwrite = LEVEL_DEBUG;
                }
            }
            if (property->isVariable) {
                if (internal) {
                    /* Never show a warning when the property is overriden internally. */
                    logLevelOnOverwrite = LEVEL_NONE;
                } else if (property->internal && (logLevelOnOverwrite < LEVEL_WARN)) {
                    /* Always show a warning when the user is overriding an internal variable. */
                    logLevelOnOverwrite = LEVEL_WARN;
                }
            }
            
            if ((getLowLogLevel() <= logLevelOnOverwrite) && (logLevelOnOverwrite != LEVEL_NONE)) {
                overwriteWarnId = 0;
                /* From version 3.5.27, the Wrapper will also log messages if the command line contains duplicated properties or attempts to set an internal environment variable. */
                if (finalValue) {
                    if (isShownAsInternal) {
                        overwriteWarnId = 1;
                    } else if (property->finalValue) {
                        overwriteWarnId = 2;
                    }
                } else {
                    if (isShownAsInternal) {
                        overwriteWarnId = 4;
                    } else if (property->finalValue) {
                        overwriteWarnId = 5;
                    } else {
                        overwriteWarnId = 6;
                    }
                }
                
                if (overwriteWarnId > 0) {
                    hidden = isSecretValue(property);
                    dispValue1 = getDisplayValue(property->value, hidden);
                    if (!dispValue1) {
                        free(propertyNameTrim);
                        free(propertyValueTrim);
                        return NULL;
                    }
                    dispValue2 = getDisplayValue(propertyValueTrim, hidden);
                    if (!dispValue2) {
                        free(dispValue1);
                        free(propertyNameTrim);
                        free(propertyValueTrim);
                        return NULL;
                    }
                    
                    switch (overwriteWarnId) {
                    case 1:
                        log_printf(WRAPPER_SOURCE_WRAPPER, logLevelOnOverwrite,
                            TEXT("The \"%s\" property is defined by the Wrapper internally and can not be overwritten.\n  Ignoring redefinition on the Wrapper command line.\n  Fixed Value %s=%s\n  Ignored Value %s=%s"),
                            propertyNameTrim, propertyNameTrim, dispValue1, propertyNameTrim, dispValue2);
                        break;

                    case 2:
                        log_printf(WRAPPER_SOURCE_WRAPPER, logLevelOnOverwrite,
                            TEXT("The \"%s\" property was already defined on the Wrapper command line and can not be overwritten.\n  Ignoring redefinition on the Wrapper command line.\n  Fixed Value %s=%s\n  Ignored Value %s=%s"),
                            propertyNameTrim, propertyNameTrim, dispValue1, propertyNameTrim, dispValue2);
                        break;

                    case 4:
                        log_printf(WRAPPER_SOURCE_WRAPPER, logLevelOnOverwrite,
                            TEXT("The \"%s\" property is defined by the Wrapper internally and can not be overwritten.\n  Ignoring redefinition on line #%d of configuration file: %s\n  Fixed Value %s=%s\n  Ignored Value %s=%s"),
                            propertyNameTrim, lineNum, (filename ? filename : TEXT("<NULL>")), propertyNameTrim, dispValue1, propertyNameTrim, dispValue2);
                        break;

                    case 5:
                        log_printf(WRAPPER_SOURCE_WRAPPER, logLevelOnOverwrite,
                            TEXT("The \"%s\" property was defined on the Wrapper command line and can not be overwritten.\n  Ignoring redefinition on line #%d of configuration file: %s\n  Fixed Value %s=%s\n  Ignored Value %s=%s"),
                            propertyNameTrim, lineNum, (filename ? filename : TEXT("<NULL>")), propertyNameTrim, dispValue1, propertyNameTrim, dispValue2);
                        break;

                    case 6:
                        log_printf(WRAPPER_SOURCE_WRAPPER, logLevelOnOverwrite,
                            TEXT("The \"%s\" property was redefined on line #%d of configuration file: %s\n  Old Value %s=%s\n  New Value %s=%s"),
                            propertyNameTrim, lineNum, (filename ? filename : TEXT("<NULL>")), propertyNameTrim, dispValue1, propertyNameTrim, dispValue2);
                        break;
                    }
                    free(dispValue1);
                    free(dispValue2);
                }
            }
            if (properties->exitOnOverwrite) {
                properties->overwrittenPropertyCausedExit = TRUE;
            }
        }
    }
    free(propertyNameTrim);

    if (setValue) {
        if (escapable && isEscapedProperty(property->name)) {
            /* Expand the value. */
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("expanding value of %s"), property->name);
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  value   : %s"), propertyValueTrim);
#endif
            propertyExpandedValue = expandEscapedCharacters(propertyValueTrim);
            if (!propertyExpandedValue) {
                free(propertyValueTrim);
                return NULL;
            }
#ifdef _DEBUG
            log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("  expanded: %s"), propertyExpandedValue);
#endif

            /* Set the property value. */
            setInnerProperty(properties, property, propertyExpandedValue, FALSE);

            free(propertyExpandedValue);
        } else {
            /* Set the property value. */
            setInnerProperty(properties, property, propertyValueTrim, FALSE);
        }

        if (property->value == NULL) {
            free(propertyValueTrim);
            return NULL;
        }
        /* Store the final flag */
        property->finalValue = finalValue;

        /* Store the quotable flag. */
        property->quotable = quotable;
        
        /* Store the internal flag. */
        property->internal = internal;
        
        /* Store the include depth. */
        property->lastDefinitionDepth = depth;

        /* Prepare the property by expanding any environment variables that are defined. */
        prepareProperty(properties, property, FALSE);
        
        /* Store the file name if any. */
        if (property->filePath != NULL) {
            free(property->filePath);
            property->filePath = NULL;
        }
        if (filename) {
            property->filePath = malloc(sizeof(TCHAR) * (_tcslen(filename) + 1));
            _tcsncpy(property->filePath, filename, _tcslen(filename) + 1);
        }
        
        /* Store the line number. */
        property->lineNumber = lineNum;

        /* See if this is a variable definition */
        if ((_tcslen(property->name) > 12) && (_tcsstr(property->name, TEXT("set.default.")) == property->name)) {
            /* This property is an environment variable definition that should only
             *  be set if the environment variable does not already exist.  Get the
             *  value back out of the property as it may have had environment
             *  replacements. */
            
            property->isVariable = TRUE;
            
            oldVal = _tgetenv(property->name + 12);
            if (oldVal == NULL) {
                /* Only set the variable if the new value is not NULL. */
                if (propertyValue) {
#ifdef _DEBUG
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("set default env('%s', '%s')"),
                        property->name + 12, property->value);
#endif
                    setEnv(property->name + 12, property->value, (internal ? ENV_SOURCE_APPLICATION : ENV_SOURCE_CONFIG));
                }
            } else {
#ifdef _DEBUG
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT(
                    "not setting default env('%s', '%s'), already set to '%s'"),
                    property->name + 12, property->value, oldVal);
#endif
#if !defined(WIN32) && defined(UNICODE)
                free(oldVal);
#endif
            }
        } else if ((_tcslen(property->name) > 4) && (_tcsstr(property->name, TEXT("set.")) == property->name)) {
            /* This property is an environment variable definition.  Get the
             *  value back out of the property as it may have had environment
             *  replacements. */
            
            property->isVariable = TRUE;
            
            if (propertyValue) {
                /* Set the variable if the new value is not NULL. */
#ifdef _DEBUG
                log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("set env('%s', '%s')"),
                    property->name + 4, property->value);
#endif
                setEnv(property->name + 4, property->value, (internal ? ENV_SOURCE_APPLICATION : ENV_SOURCE_CONFIG));
            } else {
                oldVal = _tgetenv(property->name + 4);
                if (oldVal) {
                /* Clear the variable if the new value is NULL and the environment was not NULL. */
#ifdef _DEBUG
                    log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_STATUS, TEXT("set env('%s', '<NULL>')"),
                        property->name + 4);
#endif
                    setEnv(property->name + 4, NULL, (internal ? ENV_SOURCE_APPLICATION : ENV_SOURCE_CONFIG));
#if !defined(WIN32) && defined(UNICODE)
                    free(oldVal);
#endif
                }
            }
        }
    }
    free(propertyValueTrim);

    return property;
}

/**
 * Takes a name/value pair in the form <name>=<value> and attempts to add
 * it to the specified properties table.
 *
 * @param properties Properties structure to add to.
 * @param filename Name of the file from which the property was loaded.  NULL, if not from a file.
 * @param lineNum Line number of the property declaration in the file.  Ignored if filename is NULL.
 * @param propertyNameValue The "name=value" pair to create the property from.
 * @param finalValue TRUE if the property should be set as static.
 * @param quotable TRUE if the property could contain quotes.
 * @param internal TRUE if the property is a Wrapper internal property.
 *
 * Returns 0 if successful, otherwise 1
 */
int addPropertyPair(Properties *properties, const TCHAR* filename, int lineNum, const TCHAR *propertyNameValue, int finalValue, int quotable, int internal) {
    TCHAR buffer[MAX_PROPERTY_NAME_VALUE_LENGTH];
    TCHAR *d;

    /* Make a copy of the pair that we can edit */
    if (_tcslen(propertyNameValue) + 1 >= MAX_PROPERTY_NAME_VALUE_LENGTH) {
        log_printf(WRAPPER_SOURCE_WRAPPER, LEVEL_FATAL,
            TEXT("The following property name value pair is too large.  Need to increase the internal buffer size: %s"), propertyNameValue);
        return 1;
    }
    _tcsncpy(buffer, propertyNameValue, MAX_PROPERTY_NAME_VALUE_LENGTH);

    if ((d = checkPropertySyntax(buffer))) {
        if (addProperty(properties, filename, lineNum, 0, buffer, d, finalValue, quotable, FALSE, internal) != NULL) {
            return 0;
        }
    }
    return 1;
}

/**
 * Register an internal variable and add it to the properties structure in order to get correct warnings.
 *
 * @param properties The Properties structure.
 * @param varName The variable name.
 * @param varValue The value of the variable.
 * @param finalValue TRUE if the variable can be changed after the configuration is loaded.
 * @param ignore TRUE if the variable should be added to properties->ignoreVarMap
 *               which means it should not be expanded until its value is turned to FALSE.
 */
void setInternalVarProperty(Properties *properties, const TCHAR *varName, const TCHAR *varValue, int finalValue, int ignore) {
    TCHAR* propertyName;
    
    /* A variable that was never set as ignored does not need to be added in the Hashmap.
     * A variable that was previously set as ignored can be kept in the Hashmap but we need to set the hash value to "FALSE". */
    if (ignore) {
        hashMapPutKWVW(properties->ignoreVarMap, varName, TEXT("TRUE"));
    } else if (hashMapGetKWVW(properties->ignoreVarMap, varName)) {
        hashMapPutKWVW(properties->ignoreVarMap, varName, TEXT("FALSE"));
    }
    
    /* Do not warn about this variable */
    hashMapPutKWVW(properties->warnedVarMap, varName, TEXT("INTERNAL"));
    
    propertyName = malloc(sizeof(TCHAR) * (4 + _tcslen(varName) + 1));
    if (!propertyName) {
        outOfMemory(TEXT("SIVP"), 1);
        return;
    }
    _sntprintf(propertyName, 4 + _tcslen(varName) + 1, TEXT("set.%s"), varName);
    
    /* Always add the variable as a property to prevent user from overriding it and to get the correct warning, but pass a NULL value if the variable should not be set or cleared. */
    addProperty(properties, NULL, 0, 0, propertyName, varValue, finalValue, FALSE, FALSE, TRUE);
    free(propertyName);
}

const TCHAR* getStringProperty(Properties *properties, const TCHAR *propertyName, const TCHAR *defaultValue) {
    Property *property;
    property = getInnerProperty(properties, propertyName, TRUE);
    if (property == NULL) {
        if (defaultValue != NULL) {
            property = addProperty(properties, NULL, 0, 0, propertyName, defaultValue, FALSE, FALSE, FALSE, FALSE);
            if (property) {
                property->isGenerated = TRUE;
                return property->value;
            } else {
                /* We failed to add the property, but still return the default. */
                return defaultValue;
            }
        } else {
            return NULL;
        }
    } else {
        return property->value;
    }
}

const TCHAR* getNotEmptyStringProperty(Properties *properties, const TCHAR *propertyName, const TCHAR *defaultValue) {
    const TCHAR* result = getStringProperty(properties, propertyName, defaultValue);
    if (result && (_tcslen(result) > 0)) {
        return result;
    }
    return defaultValue;
}

const TCHAR* getFileSafeStringProperty(Properties *properties, const TCHAR *propertyName, const TCHAR *defaultValue) {
    Property *property;
    TCHAR *buffer;
    int i;

    property = getInnerProperty(properties, propertyName, TRUE);
    if (property == NULL) {
        if (defaultValue != NULL) {
            property = addProperty(properties, NULL, 0, 0, propertyName, defaultValue, FALSE, FALSE, FALSE, FALSE);
            if (property) {
                property->isGenerated = TRUE;
            }
        }

        return defaultValue;
    } else {
        buffer = property->value;
        if (_tcschr(buffer, TEXT('%'))) {
            i = 0;
            while (buffer[i]) {
                if (buffer[i] == TEXT('%')) {
                    buffer[i] = TEXT('_');
                }
                i++;
            }
        }
        return buffer;
    }
}

/**
 * Does a quick sort of the property values, keeping the values together.
 */
void sortStringProperties(long unsigned int *propertyIndices, TCHAR **propertyNames, TCHAR **propertyValues, int low, int high) {
    int i = low;
    int j = high;
    long int tempIndex;
    TCHAR *tempName;
    TCHAR *tempValue;
    long unsigned int x = propertyIndices[(low + high) / 2];

    do {
        while (propertyIndices[i] < x) {
            i++;
        }
        while (propertyIndices[j] > x) {
            j--;
        }
        if (i <= j) {
            /* Swap i and j values. */
            tempIndex = propertyIndices[i];
            tempName = propertyNames[i];
            tempValue = propertyValues[i];

            propertyIndices[i] = propertyIndices[j];
            propertyNames[i] = propertyNames[j];
            propertyValues[i] = propertyValues[j];

            propertyIndices[j] = tempIndex;
            propertyNames[j] = tempName;
            propertyValues[j] = tempValue;

            i++;
            j--;
        }
    } while (i <= j);

    /* Recurse */
    if (low < j) {
        sortStringProperties(propertyIndices, propertyNames, propertyValues, low, j);
    }
    if (i < high) {
        sortStringProperties(propertyIndices, propertyNames, propertyValues, i, high);
    }
}

/**
 * Returns a sorted array of all properties beginning with {propertyNameBase}.
 *  Only numerical characters can be returned between the two.
 *
 * The calling code must always call freeStringProperties to make sure that the
 *  malloced propertyNames, propertyValues, and propertyIndices arrays are freed
 *  up correctly.  This is only necessary if the function returns 0.
 *
 * @param properties The full properties structure.
 * @param propertyNameHead All matching properties must begin with this value.
 * @param propertyNameTail All matching properties must end with this value.
 * @param all If FALSE then the array will start with #1 and loop up until the
 *            next property is not found, if TRUE then all properties will be
 *            returned, even if there are gaps in the series.
 * @param matchAny If FALSE only numbers are allowed as placeholder,
 *                 If TRUE any strings (including empty string) are allowed and
 *                 propertyIndices is not set.
 * @param propertyNames Returns a pointer to a NULL terminated array of
 *                      property names.
 * @param propertyValues Returns a pointer to a NULL terminated array of
 *                       property values.
 * @param propertyIndices Returns a pointer to a 0 terminated array of
 *                        the index numbers used in each property name of
 *                        the propertyNames array.
 *
 * @return 0 if successful, -1 if there was an error.
 */
int getStringProperties(Properties *properties, const TCHAR *propertyNameHead, const TCHAR *propertyNameTail, int all, int matchAny, TCHAR ***propertyNames, TCHAR ***propertyValues, long unsigned int **propertyIndices) {
    int j;
    int k;
    size_t headLen;
    size_t tailLen;
    size_t thisLen;
    TCHAR *thisHead;
    TCHAR *thisTail;
    size_t i;
    Property *property;
    size_t indexLen;
    TCHAR indexS[16];
    int ok;
    TCHAR c;
    int count;
    int firstPass = TRUE;

    if (!matchAny) {
        *propertyIndices = NULL;
    }

    headLen = _tcslen(propertyNameHead);
    tailLen = _tcslen(propertyNameTail);

    for (j = 0; j < 2; j++) {
        count = 0;
        property = properties->first;
        while (property != NULL) {
            thisLen = _tcslen(property->name);
            if (matchAny && (thisLen < headLen + tailLen)) {
                /* Too short, not what we are looking for. */
            } else if (!matchAny && (thisLen < headLen + tailLen + 1)) {
                /* Too short, not what we are looking for. */
            } else {
                thisHead = malloc(sizeof(TCHAR) * (headLen + 1));
                if (!thisHead) {
                    outOfMemory(TEXT("GSPS"), 1);
                } else {
                    _tcsncpy(thisHead, property->name, headLen);
                    thisHead[headLen] = 0;

                    if (strcmpIgnoreCase(thisHead, propertyNameHead) == 0) {
                        /* Head matches. */

                        thisTail = malloc(sizeof(TCHAR) * (tailLen + 1));
                        if (!thisTail) {
                            outOfMemory(TEXT("GSPS"), 2);
                        } else {
                            _tcsncpy(thisTail, property->name + thisLen - tailLen, tailLen + 1);

                            if (strcmpIgnoreCase(thisTail, propertyNameTail) == 0) {
                                /* Tail matches. */
                                indexLen = thisLen - headLen - tailLen;
                                if (indexLen <= 15) {
                                    ok = TRUE;
                                    
                                    if (!matchAny) {
                                        _tcsncpy(indexS, property->name + headLen, indexLen);
                                        indexS[indexLen] = 0;

                                        for (i = 0; i < indexLen; i++) {
                                            c = indexS[i];
                                            if ((c < '0') || (c > '9')) {
                                                ok = FALSE;
                                                break;
                                            }
                                        }
                                    }

                                    if (ok) {
                                        if (!firstPass) {
                                            prepareProperty(properties, property, FALSE);

                                            if (!matchAny) {
                                                (*propertyIndices)[count] = _tcstoul(indexS, NULL, 10);
                                            }
                                            (*propertyNames)[count] = property->name;
                                            (*propertyValues)[count] = property->value;
                                        }

                                        count++;
                                    }
                                }
                            }

                            free(thisTail);
                        }
                    }

                    free(thisHead);
                }
            }

            /* Keep looking */
            property = property->next;
        }

        if (firstPass) {
            firstPass = FALSE;

            *propertyNames = malloc(sizeof(TCHAR *) * (count + 1));
            if (!(*propertyNames)) {
                outOfMemory(TEXT("GSPS"), 3);
                *propertyNames = NULL;
                *propertyValues = NULL;
                if (!matchAny) {
                    *propertyIndices = NULL;
                }
                return -1;
            }

            *propertyValues = malloc(sizeof(TCHAR *) * (count + 1));
            if (!(*propertyValues)) {
                outOfMemory(TEXT("GSPS"), 4);
                free(*propertyNames);
                *propertyNames = NULL;
                *propertyValues = NULL;
                if (!matchAny) {
                    *propertyIndices = NULL;
                }
                return -1;
            }

            if (!matchAny) {
                *propertyIndices = malloc(sizeof(long unsigned int) * (count + 1));
                if (!(*propertyIndices)) {
                    outOfMemory(TEXT("GSPS"), 5);
                    free(*propertyNames);
                    free(*propertyValues);
                    *propertyNames = NULL;
                    *propertyValues = NULL;
                    *propertyIndices = NULL;
                    return -1;
                }
            }

            if (count == 0) {
                /* The count is 0 so no need to continue through the loop again. */
                (*propertyNames)[0] = NULL;
                (*propertyValues)[0] = NULL;
                if (!matchAny) {
                    (*propertyIndices)[0] = 0;
                }
                return 0;
            }
        } else {
            /* Second pass */
            (*propertyNames)[count] = NULL;
            (*propertyValues)[count] = NULL;
            if (!matchAny) {
                (*propertyIndices)[count] = 0;

                sortStringProperties(*propertyIndices, *propertyNames, *propertyValues, 0, count - 1);

                /* If we don't want all of the properties then we need to remove the extra ones.
                 *  Names and values are not allocated, so setting them to NULL is fine.*/
                if (!all) {
                    for (k = 0; k < count; k++) {
                        if ((*propertyIndices)[k] != k + 1) {
                            (*propertyNames)[k] = NULL;
                            (*propertyValues)[k] = NULL;
                            (*propertyIndices)[k] = 0;
                        }
                    }
                }
            }
            /*
            for (k = 0; k < count; k++) {
                if ((*propertyNames)[k]) {
                    _tprintf("[%d] #%lu: %s=%s\n", k, (*propertyIndices)[k], (*propertyNames)[k], (*propertyValues)[k]);
                }
            }
            */

            return 0;
        }
    }

    /* For compiler */
    return 0;
}

/**
 * Frees up an array of properties previously returned by getStringProperties().
 */
void freeStringProperties(TCHAR **propertyNames, TCHAR **propertyValues, long unsigned int *propertyIndices) {
    /* The property names are not malloced. */
    free(propertyNames);

    /* The property values are not malloced. */
    free(propertyValues);

    free(propertyIndices);
}

int getIntProperty(Properties *properties, const TCHAR *propertyName, int defaultValue) {
    TCHAR buffer[16];
    Property *property;
    int i;
    TCHAR c;
    int value;

    property = getInnerProperty(properties, propertyName, TRUE);
    if (property == NULL) {
        _sntprintf(buffer, 16, TEXT("%d"), defaultValue);
        property = addProperty(properties, NULL, 0, 0, propertyName, buffer, FALSE, FALSE, FALSE, FALSE);
        if (property) {
            property->isGenerated = TRUE;
        }

        return defaultValue;
    } else {
        value = (int)_tcstol(property->value, NULL, 0);
        
        /* Make sure that the property does not contain invalid characters. */
        i = 0;
        do {
            c = property->value[i];
            if ((i > 0) && (c == TEXT('\0'))) {
                /* Fall through */
            } else if ((i == 0) && (c == TEXT('-'))) {
                /* Negative number.  This is Ok. */
            } else if ((c < TEXT('0')) || (c > TEXT('9'))) {
                if (i == 0) {
                    /* If the bad character is the first character then use the default value. */
                    value = defaultValue;
                }
                
                if (properties->logWarnings) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, properties->logWarningLogLevel,
                        TEXT("Encountered an invalid numerical value for configuration property %s=%s.  Resolving to %d."),
                        propertyName, property->value, value);
                }
                
                break;
            }
            i++;
        } while (c != TEXT('\0'));
        
        return value;
    }
}

int getBooleanProperty(Properties *properties, const TCHAR *propertyName, int defaultValue) {
    const TCHAR *defaultValueS;
    Property *property;
    const TCHAR *propertyValue;
    
    if (defaultValue) {
        defaultValueS = TEXT("TRUE");
    } else {
        defaultValueS = TEXT("FALSE");
    }

    property = getInnerProperty(properties, propertyName, TRUE);
    if (property == NULL) {
        property = addProperty(properties, NULL, 0, 0, propertyName, defaultValueS, FALSE, FALSE, FALSE, FALSE);
        if (property) {
            property->isGenerated = TRUE;
        }
        propertyValue = defaultValueS;
    } else {
        propertyValue = property->value;
    }
    
    if (strcmpIgnoreCase(propertyValue, TEXT("TRUE")) == 0) {
        return TRUE;
    } else if (strcmpIgnoreCase(propertyValue, TEXT("FALSE")) == 0) {
        return FALSE;
    } else {
        if (properties->logWarnings) {
            log_printf(WRAPPER_SOURCE_WRAPPER, properties->logWarningLogLevel,
                TEXT("Encountered an invalid boolean value for configuration property %s=%s.  Resolving to %s."),
                propertyName, propertyValue, defaultValueS);
        }
        
        return defaultValue;
    }
}

/**
 * Build sorted arrays of all boolean properties beginning with {propertyNameBase}.
 *  Only numerical characters can be returned between the two.
 *
 * The calling code must always call freeBooleanProperties to make sure that the
 *  malloced propertyNames, propertyValues, and propertyIndices arrays are freed
 *  up correctly.  This is only necessary if the function returns 0.
 *
 * @param properties The full properties structure.
 * @param propertyNameHead All matching properties must begin with this value.
 * @param propertyNameTail All matching properties must end with this value.
 * @param all If FALSE then the array will start with #1 and loop up until the
 *            next property is not found, if TRUE then all properties will be
 *            returned, even if there are gaps in the series.
 * @param matchAny If FALSE only numbers are allowed as placeholder
 * @param propertyNames Returns a pointer to a NULL terminated array of
 *                      property names.
 * @param propertyValues Returns a pointer to a NULL terminated array of
 *                       property values.
 * @param propertyIndices Returns a pointer to a 0 terminated array of
 *                        the index numbers used in each property name of
 *                        the propertyNames array.
 *
 * @return 0 if successful, -1 if there was an error.
 */
int getBooleanProperties(Properties *properties, const TCHAR *propertyNameHead, const TCHAR *propertyNameTail, int all, int matchAny, TCHAR ***propertyNames, int **propertyValues, long unsigned int **propertyIndices, int defaultValue) {
    TCHAR **strPropertyValues;
    int i = 0;
    int count = 0;
    int result;
    
    result = getStringProperties(properties, propertyNameHead, propertyNameTail, all, matchAny, propertyNames, &strPropertyValues, propertyIndices);
    if (result == -1)
        return result;
    
    while (strPropertyValues[i]) {
        count++;
        i++;
    }
    *propertyValues = malloc(sizeof(TCHAR *) * (count + 1));
    
    i = 0;
    while (strPropertyValues[i]) {
        if (strcmpIgnoreCase(strPropertyValues[i], TEXT("TRUE")) == 0) {
            (*propertyValues)[i] = TRUE;
        } else if (strcmpIgnoreCase(strPropertyValues[i], TEXT("FALSE")) == 0) {
            (*propertyValues)[i] = FALSE;
        } else {
            if (properties->logWarnings) {
                log_printf(WRAPPER_SOURCE_WRAPPER, properties->logWarningLogLevel,
                    TEXT("Encountered an invalid boolean value for configuration property %s=%s.  Resolving to %s."),
                    (*propertyNames)[i], strPropertyValues[i], defaultValue ? TEXT("TRUE") : TEXT("FALSE"));
            }
            
            (*propertyValues)[i] = defaultValue;
        }
        i++;    
    }
    
    free(strPropertyValues);
    return 0;
}

void freeBooleanProperties(TCHAR **propertyNames, int *propertyValues, long unsigned int *propertyIndices) {
    /* The property names are not malloced. */
    free(propertyNames);

    free(propertyValues);

    free(propertyIndices);
}

static const TCHAR* getStatusStr(int status) {
    TCHAR *name;
    switch (status) {
    case STATUS_ENABLED:
        name = TEXT("ENABLED");
        break;

    case STATUS_DISABLED:
        name = TEXT("DISABLED");
        break;

    default:
        name = TEXT("UNCHANGED");
        break;
    }
    return name;
}

int getStatusProperty(Properties *properties, const TCHAR *propertyName, int defaultValue) {
    const TCHAR* valueStr = getStringProperty(properties, propertyName, NULL);
    
    if (valueStr) {
        if (strcmpIgnoreCase(valueStr, TEXT("UNCHANGED")) == 0) {
            return STATUS_UNCHANGED;
        } else if (strcmpIgnoreCase(valueStr, TEXT("ENABLED")) == 0) {
            return STATUS_ENABLED;
        } else if (strcmpIgnoreCase(valueStr, TEXT("DISABLED")) == 0) {
            return STATUS_DISABLED;
        } else if (properties->logWarnings) {
            log_printf(WRAPPER_SOURCE_WRAPPER, properties->logWarningLogLevel,
                TEXT("Encountered an invalid value for configuration property %s=%s.  Resolving to %s."),
                propertyName, valueStr, getStatusStr(defaultValue));
        }
    }
    return defaultValue;
}

/**
 * Indicates if a property was generated by the Wrapper or written in the configuration.
 *  ATTENTION: The value returned by this function should never be used in a condition
 *             that will affect the Wrapper behaviour. We want the configuration to be
 *             loaded the same way if a property is set with a default value or not set.
 *             This function should only be used for logging purpose, for example to print
 *             a property name making sure it actually exists in the configuration file.
 *
 * @param properties The full properties structure.
 * @param propertyName The name of the property to check.
 *
 * @return TRUE if the property was generated by the Wrapper, FALSE if it exists in the configuration.
 */
int isGeneratedProperty(Properties *properties, const TCHAR *propertyName) {
    Property *property;
    property = getInnerProperty(properties, propertyName, FALSE);
    if (property == NULL) {
        return FALSE;
    } else {
        return property->isGenerated;
    }
}

int isQuotableProperty(Properties *properties, const TCHAR *propertyName) {
    Property *property;
    property = getInnerProperty(properties, propertyName, FALSE);
    if (property == NULL) {
        return FALSE;
    } else {
        return property->quotable;
    }
}

/**
 * Return a code indicating how the property can be dumped.
 *
 * @property property to check.
 *
 * @return 0 if the property should not be dumped.
 *         1 if the property can be logged normally.
 */
int propertyDumpFilter(Property *property) {
    if (property->isGenerated || property->internal) {
        return 0;
    }
/*
    if (_tcsstr(propName, TEXT(".license."))) {
        free(propName);
        return 0;
    }
*/
    return 1;
}

const TCHAR* getPropertySourceName(Property *property) {
    if (property->finalValue) {
        return TEXT("COMMAND ");
    } else if (property->isGenerated) {
        return TEXT("WRAPPER ");
    } else {
        return TEXT("FILE    ");
    }
}

TCHAR getPropertySourceShortName(Property *property) {
    if (property->finalValue) {
        return TEXT('C');
    } else if (property->isGenerated) {
        return TEXT('W');
    } else {
        return TEXT('F');
    }
}

/* Returns the number of columns with variable sizes and the required size. */
int getColumnsAndReqVarSizeForPropertyDump(TCHAR* value, TCHAR* format, size_t *reqSize) {
    int numColumns;
    int i;

    for(i = 0, numColumns = 0; i < (int)_tcslen(format); i++ ) {
        switch(format[i]) {
        case TEXT('V'):
        case TEXT('v'):
            *reqSize += _tcslen(value) + 3;
            numColumns++;
            break;
        }
    }
    return numColumns;
}

/* Returns the number of columns with constant sizes, and retrieve the sizes that should be calculated taking into account all properties. */
int getColumnsAndReqConstSizesForPropertyDump(Properties *properties, TCHAR* format, size_t *reqSize, size_t *reqPropNameSize, size_t *reqConfPathSize, size_t *reqConfNameSize) {
    Property *property;
    int dumpFilter;
    int addPropNameSize = FALSE;
    int addConfPathSize = FALSE;
    int addConfNameSize = FALSE;
    int numColumns;
    int i;

    *reqSize = 0;
    *reqPropNameSize = 0;
    *reqConfPathSize = 0;
    *reqConfNameSize = 0;
    for(i = 0, numColumns = 0; i < (int)_tcslen(format); i++ ) {
        switch(format[i]) {
        case TEXT('S'):
        case TEXT('s'):
            *reqSize += 1 + 3;
            numColumns++;
            break;

        case TEXT('Z'):
        case TEXT('z'):
            *reqSize += 8 + 3; /* FILE|EMBEDDED|COMMAND|WRAPPER */
            numColumns++;
            break;

        case TEXT('F'):
        case TEXT('f'):
            *reqSize += 1 + 3;
            numColumns++;
            break;

        case TEXT('P'):
        case TEXT('p'):
            addConfPathSize = TRUE;
            numColumns++;
            break;

        case TEXT('C'):
        case TEXT('c'):
            addConfNameSize = TRUE;
            numColumns++;
            break;

        case TEXT('L'):
        case TEXT('l'):
            *reqSize += 4 + 3;
            numColumns++;
            break;

        case TEXT('I'):
        case TEXT('i'):
            *reqSize += 1 + 3;
            numColumns++;
            break;

        case TEXT('N'):
        case TEXT('n'):
            addPropNameSize = TRUE;
            numColumns++;
            break;
        }
    }
    if (addPropNameSize || addConfPathSize || addConfNameSize) {
        property = properties->first;
        while (property != NULL) {
            dumpFilter = propertyDumpFilter(property);
            if (dumpFilter > 0) {
                if (addPropNameSize) {
                    *reqPropNameSize = __max(_tcslen(property->name), *reqPropNameSize);
                }
                /* We assume that the conf file name and path are written with characters that display on a single char width.
                 *  We would need a smarter function to calculate the length of strings containing full width Japanese characters. */
                if (addConfPathSize && property->filePath) {
                    *reqConfPathSize = __max(_tcslen(property->filePath), *reqConfPathSize);
                }
                if (addConfNameSize && property->filePath) {
                    *reqConfNameSize = __max(_tcslen(getFileName(property->filePath)), *reqConfNameSize);
                }
            }
            property = property->next;
        }
        if (addPropNameSize) {
            *reqSize += *reqPropNameSize + 3;
        }
        if (addConfPathSize) {
            *reqSize += *reqConfPathSize + 3;
        }
        if (addConfNameSize) {
            *reqSize += *reqConfNameSize + 3;
        }
    }
    return numColumns;
}

TCHAR* buildPropertyDumpBuffer(Property *property, TCHAR* format, int numConstColumns, size_t reqConstTotSize, size_t reqPropNameSize, size_t reqConfPathSize, size_t reqConfNameSize) {
    int       i;
    size_t    reqSize;
    int       numColumns;
    TCHAR     *pos;
    int       currentColumn;
    int       handledFormat;
    int       temp = 0;
    int       len = 0;
    TCHAR*    printBuffer;
    TCHAR*    propValue;
#if defined(UNICODE) && !defined(WIN32)
    const TCHAR* leftAlignStrFormat = TEXT("%-*S");
#else
    const TCHAR* leftAlignStrFormat = TEXT("%-*s");
#endif

    propValue = getDisplayValue(property->value, isSecretValue(property));
    if (!propValue) {
        return NULL;
    }
    
    /* The required size for the columns with a constant width, as well as their number, are calculated once and for all properties. */
    reqSize = reqConstTotSize;
    numColumns = numConstColumns;
    
    /* Then add the variable columns. */
    numColumns += getColumnsAndReqVarSizeForPropertyDump(propValue, format, &reqSize);
    
    if (reqSize == 0) {
        free(propValue);
        /* Invalid format - this should not happen because we checked that the format was correct before calling this function. */
        return NULL;
    }

    /* Always add room for the null. */
    reqSize += 1;
    
    printBuffer = malloc(sizeof(TCHAR) * reqSize);
    if (!printBuffer) {
        outOfMemory(TEXT("BPDB"), 1);
        free(propValue);
        return NULL;
    }
    pos = printBuffer;

    /* Indent with two characters to display like when dumping environment variables. */
    temp = _sntprintf(pos, 3, TEXT("  "));
    pos += temp;
    len += temp;
    reqSize += len;

    for(i = 0, currentColumn = 0; i < (int)_tcslen(format); i++) {
        handledFormat = TRUE;

        switch(format[i]) {
        case TEXT('S'):
        case TEXT('s'):
            temp = _sntprintf(pos, reqSize - len, TEXT("%c"), getPropertySourceShortName(property));
            currentColumn++;
            break;

        case TEXT('Z'):
        case TEXT('z'):
            temp = _sntprintf(pos, reqSize - len, TEXT("%s"), getPropertySourceName(property));
            currentColumn++;
            break;

        case TEXT('F'):
        case TEXT('f'):
            temp = _sntprintf(pos, reqSize - len, TEXT("%c"), (property->finalValue ? TEXT('F') : TEXT(' ')));
            currentColumn++;
            break;

        case TEXT('P'):
        case TEXT('p'):
            temp = _sntprintf(pos, reqSize - len, leftAlignStrFormat, reqConfPathSize, property->filePath ? property->filePath : TEXT(""));
            currentColumn++;
            break;

        case TEXT('C'):
        case TEXT('c'):
            temp = _sntprintf(pos, reqSize - len, leftAlignStrFormat, reqConfNameSize, property->filePath ? getFileName(property->filePath) : TEXT(""));
            currentColumn++;
            break;

        case TEXT('L'):
        case TEXT('l'):
            if (getPropertySourceShortName(property) != TEXT('F')) {
                temp = _sntprintf(pos, reqSize - len, TEXT("    "));
            } else if (property->lineNumber > 9999) {
                temp = _sntprintf(pos, reqSize - len, TEXT("****"));
            } else {
                temp = _sntprintf(pos, reqSize - len, TEXT("%4d"), property->lineNumber);
            }
            currentColumn++;
            break;

        case TEXT('I'):
        case TEXT('i'):
            if (property->definitions > 9) {
                temp = _sntprintf(pos, reqSize - len, TEXT("*"));
            } else if (property->definitions > 1) {
                /* Only show the number of definitions if it's more than 1, to make those cases appear clearly. */
                temp = _sntprintf(pos, reqSize - len, TEXT("%d"), property->definitions);
            } else {
                /* In the future we may want to show a '-' if the property is not defined anywhere (0 definitions). For now it is just not listed. */
                temp = _sntprintf(pos, reqSize - len, TEXT(" "));
            }
            currentColumn++;
            break;

        case TEXT('N'):
        case TEXT('n'):
            temp = _sntprintf(pos, reqSize - len, leftAlignStrFormat, reqPropNameSize, property->name);
            currentColumn++;
            break;

        case TEXT('V'):
        case TEXT('v'):
            temp = _sntprintf(pos, reqSize - len, TEXT("%s"), propValue);
            currentColumn++;
            break;

        default:
            handledFormat = FALSE;
        }
        
        if (handledFormat) {
            pos += temp;
            len += temp;
            
            /* Add separator chars */
            if (currentColumn != numColumns) {
                temp = _sntprintf(pos, reqSize - len, TEXT(" | "));
                pos += temp;
                len += temp;
            }
        }
    }
    free(propValue);

    /* Return the print buffer to the caller. */
    return printBuffer;
}

void dumpProperties(Properties *properties) {
    Property *property;
    int dumpFilter;
    int numConstColumns;
    size_t reqConstTotSize;
    size_t reqPropNameSize;
    size_t reqConfPathSize;
    size_t reqConfNameSize;
    TCHAR* printBuffer;
    
    if ((getLowLogLevel() <= properties->dumpLogLevel) && (properties->dumpLogLevel != LEVEL_NONE)) {
        property = properties->first;
        
        /* The required size for the columns with a constant width, as well as their number, are calculated once and for all properties. */
        numConstColumns = getColumnsAndReqConstSizesForPropertyDump(properties, properties->dumpFormat, &reqConstTotSize, &reqPropNameSize, &reqConfPathSize, &reqConfNameSize);
        
        if ((numConstColumns == 0) && (getColumnsAndReqVarSizeForPropertyDump(TEXT(""), properties->dumpFormat, &reqConstTotSize) == 0)) {
            /* No columns or invalid format - use the default format instead. */
            log_printf(WRAPPER_SOURCE_WRAPPER, properties->logWarningLogLevel,
                TEXT("Encountered an invalid format for configuration property %s=%s.  Resolving to '%s'."),
                TEXT("wrapper.properties.dump.format"),
                properties->dumpFormat,
                PROPERTIES_DUMP_FORMAT_DEFAULT);
            setPropertiesDumpFormat(properties, PROPERTIES_DUMP_FORMAT_DEFAULT);
            
            /* Recalculate the size for the columns with a constant width and their number. */
            numConstColumns = getColumnsAndReqConstSizesForPropertyDump(properties, properties->dumpFormat, &reqConstTotSize, &reqPropNameSize, &reqConfPathSize, &reqConfNameSize);
        }
        
        log_printf(WRAPPER_SOURCE_WRAPPER, properties->dumpLogLevel, TEXT(""));
        log_printf(WRAPPER_SOURCE_WRAPPER, properties->dumpLogLevel, TEXT("Wrapper configuration properties BEGIN:"));
        
        while (property != NULL) {
            dumpFilter = propertyDumpFilter(property);
            if (dumpFilter > 0) {
                printBuffer = buildPropertyDumpBuffer(property, properties->dumpFormat, numConstColumns, reqConstTotSize, reqPropNameSize, reqConfPathSize, reqConfNameSize);
                if (printBuffer) {
                    log_printf(WRAPPER_SOURCE_WRAPPER, properties->dumpLogLevel, TEXT("%s"), printBuffer);
                    free(printBuffer);
                }
            }
            property = property->next;
        }
        log_printf(WRAPPER_SOURCE_WRAPPER, properties->dumpLogLevel, TEXT("Wrapper configuration properties END:"));
        log_printf(WRAPPER_SOURCE_WRAPPER, properties->dumpLogLevel, TEXT(""));
    }
}

/**
 * Level at which properties will be dumped.
 */
void setPropertiesDumpLogLevel(Properties *properties, int logLevel) {
    properties->dumpLogLevel = logLevel;
}

/**
 * Format used when dumping properties.
 */
void setPropertiesDumpFormat(Properties *properties, const TCHAR* format) {
    if (properties->dumpFormat) {
        free(properties->dumpFormat);
    }
    properties->dumpFormat = malloc(sizeof(TCHAR) * (_tcslen(format) + 1));
    _tcsncpy(properties->dumpFormat, format, (_tcslen(format) + 1));
}

/**
 * Set to TRUE if warnings about property values should be logged.
 */
void setLogPropertyWarnings(Properties *properties, int logWarnings) {
    properties->logWarnings = logWarnings;
}


/**
 * Level at which any property warnings are logged.
 */
void setLogPropertyWarningLogLevel(Properties *properties, int logLevel) {
    properties->logWarningLogLevel = logLevel;
}

/**
 * Returns the minimum value. This is used in place of the __min macro when the parameters should not be called more than once.
 */
int propIntMin(int value1, int value2) {
    if (value1 < value2) {
        return value1;
    } else {
        return value2;
    }
}

/**
 * Returns the maximum value. This is used in place of the __max macro when the parameters should not be called more than once.
 */
int propIntMax(int value1, int value2) {
    if (value1 > value2) {
        return value1;
    } else {
        return value2;
    }
}

/** Creates a linearized representation of all of the properties.
 *  The returned buffer must be freed by the calling code. */
TCHAR *linearizeProperties(Properties *properties, TCHAR separator) {
    Property *property;
    size_t size;
    TCHAR *c;
    TCHAR *fullBuffer;
    TCHAR *work, *buffer;

    /* First we need to figure out how large a buffer will be needed to linearize the properties. */
    size = 0;
    property = properties->first;
    while (property != NULL) {
        /* Add the length of the basic property. */
        size += _tcslen(property->name);
        size++; /* '=' */
        size += _tcslen(property->value);

        /* Handle any characters that will need to be escaped. */
        c = property->name;
        while ((c = _tcschr(c, separator)) != NULL) {
            size++;
            c++;
        }
        c = property->value;
        while ((c = _tcschr(c, separator)) != NULL) {
            size++;
            c++;
        }

        size++; /* separator */

        property = property->next;
    }
    size++; /* null terminated. */

    /* Now that we know how much space this will all take up, allocate a buffer. */
    fullBuffer = buffer = calloc(sizeof(TCHAR) , size);
    if (!fullBuffer) {
        outOfMemory(TEXT("LP"), 1);
        return NULL;
    }

    /* Now actually build up the output.  Any separator characters need to be escaped with themselves. */
    property = properties->first;
    while (property != NULL) {
        /* name */
        work = property->name;
        while ((c = _tcschr(work, separator)) != NULL) {
            _tcsncpy(buffer, work, c - work + 1);
            buffer += c - work + 1;
            buffer[0] = separator;
            buffer++;
            work = c + 1;
        }
        _tcsncpy(buffer, work, size - _tcslen(fullBuffer));
        buffer += _tcslen(work);

        /* equals */
        buffer[0] = TEXT('=');
        buffer++;

        /* value */
        work = property->value;
        while ((c = _tcschr(work, separator)) != NULL) {
            _tcsncpy(buffer, work, c - work + 1);
            buffer += c - work + 1;
            buffer[0] = separator;
            buffer++;
            work = c + 1;
        }
        _tcsncpy(buffer, work, size - _tcslen(fullBuffer));
        buffer += _tcslen(work);

        /* separator */
        buffer[0] = separator;
        buffer++;

        property = property->next;
    }

    /* null terminate. */
    buffer[0] = 0;
    buffer++;

    return fullBuffer;
}
