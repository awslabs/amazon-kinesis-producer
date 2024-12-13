/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package software.amazon.kinesis.producer;

import org.apache.commons.io.Charsets;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class LogInputStreamReader implements Runnable {
    private static final Logger log = LoggerFactory.getLogger(LogInputStreamReader.class);
    private static final Pattern LEVEL_REGEX = Pattern.compile(
            "\\[(?<level>trace|debug|info|warn(?:ing)?|error|fatal)\\]", Pattern.CASE_INSENSITIVE | Pattern.MULTILINE);

    private static final Map<String, LoggingFunction> EMITTERS = makeEmitters();

    private static Map<String, LoggingFunction> makeEmitters() {

        Map<String, LoggingFunction> emitters = new HashMap<>();
        emitters.put("trace", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.info(message);
            }
        });
        emitters.put("debug", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.info(message);
            }
        });
        emitters.put("info", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.info(message);
            }
        });
        emitters.put("warn", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.warn(message);
            }
        });
        emitters.put("warning", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.warn(message);
            }
        });
        emitters.put("error", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.error(message);
            }
        });
        emitters.put("fatal", new LoggingFunction() {
            @Override
            public void apply(String message) {
                log.error(message);
            }
        });

        return Collections.unmodifiableMap(emitters);
    }

    private final String streamType;
    private final BufferedReader reader;
    private final DefaultLoggingFunction logFunction;
    private volatile boolean running = true;
    private volatile boolean shuttingDown = false;

    private boolean isReadingRecord = false;
    private final LinkedList<String> messageData = new LinkedList<>();

    public LogInputStreamReader(InputStream is, String streamType, DefaultLoggingFunction logFunction) {
        this.streamType = streamType;
        this.reader = new BufferedReader(new InputStreamReader(is, Charsets.US_ASCII));
        this.logFunction = logFunction;
    }

    @Override
    public void run() {
        while (running) {
            String logLine;
            try {
                logLine = reader.readLine();
                // log.info("Considering({}): {}", streamType, logLine);
                if (logLine == null) {
                    continue;
                }
                if (logLine.startsWith("++++")) {
                    startRead();
                } else if (logLine.startsWith("----")) {
                    finishRead();
                } else if (isReadingRecord) {
                    messageData.add(logLine);
                } else {
                    logFunction.apply(log, logLine);
                }
            } catch (IOException ioex) {
                if (shuttingDown) {
                    //
                    // Since the Daemon calls destroy instead of letting the process exit normally
                    // the input streams coming from the process will end up truncated.
                    // When we know the process is shutting down we can report the exception as info
                    //
                    if (ioex.getMessage() == null || !ioex.getMessage().contains("Stream closed")) {
                        //
                        // If the message is "Stream closed" we can safely ignore it. This is probably a bug
                        // with the UNIXProcess#ProcessPipeInputStream that it throws the exception. There
                        // is no other way to detect the other side of the request being closed.
                        //
                        log.info("Received IO Exception during shutdown.  This can happen, but should indicate "
                                + "that the stream has been closed: {}", ioex.getMessage());

                    }
                } else {
                    log.error("Caught IO Exception while reading log line", ioex);
                }

            }
        }
        if (!messageData.isEmpty()) {
            logFunction.apply(log, makeMessage());
        }
    }

    private void finishRead() {
        if (!isReadingRecord) {
            log.warn("{}: Terminator encountered, but wasn't reading record.", streamType);
        }
        isReadingRecord = false;
        if (!messageData.isEmpty()) {
            String message = makeMessage();
            getLevelOrDefault(message).apply(message);
        } else {
            log.warn("{}: Finished reading record, but didn't find any message data.", streamType);
        }
        messageData.clear();
    }

    private void startRead() {
        isReadingRecord = true;
        if (!messageData.isEmpty()) {
            log.warn("{}: New log record started, but message data has existing data: {}", streamType, makeMessage());
            messageData.clear();
        }
    }

    private LoggingFunction getLevelOrDefault(String message) {
        Matcher matcher = LEVEL_REGEX.matcher(message);
        if (matcher.find()) {
            String level = matcher.group("level");
            if (level != null) {
                LoggingFunction res = EMITTERS.get(level.toLowerCase());
                if (res != null) {
                    return res;
                }
            }
        }
        return new LoggingFunction() {
            @Override
            public void apply(String message) {
                logFunction.apply(log, "!!Failed to extract level!! - " + message);
            }
        };
    }

    private String makeMessage() {
        return StringUtils.join(messageData, "\n");
    }

    public void shutdown() {
        this.running = false;
    }

    public void prepareForShutdown() {
        this.shuttingDown = true;
    }

    static interface LoggingFunction {
        void apply(String message);
    }

    static interface DefaultLoggingFunction {
        void apply(Logger logger, String message);
    }

}
