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

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ScheduledExecutorService;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class FileAgeManagerTest {

    @Mock
    private ScheduledExecutorService executorService;

    @Mock
    private File testFile;

    @Test
    public void simpleTest() {
        FileAgeManager manager = new FileAgeManager(executorService);
        List<File> files = makeFileList();
        manager.registerFiles(files);

        verify(testFile).getAbsoluteFile();
        when(testFile.exists()).thenReturn(true);
        when(testFile.setLastModified(anyLong())).thenReturn(true);
        manager.run();

        verify(testFile).exists();
        verify(testFile).setLastModified(anyLong());
    }

    private List<File> makeFileList() {
        List<File> files = new ArrayList<>();
        files.add(testFile);
        when(testFile.getAbsoluteFile()).thenReturn(testFile);
        return files;
    }

    @Test
    public void missingFileTest() {
        FileAgeManager manager = new FileAgeManager(executorService);
        manager.registerFiles(makeFileList());

        when(testFile.exists()).thenReturn(false);
        manager.run();

        verify(testFile).exists();
        verify(testFile, never()).setLastModified(anyLong());

    }

}