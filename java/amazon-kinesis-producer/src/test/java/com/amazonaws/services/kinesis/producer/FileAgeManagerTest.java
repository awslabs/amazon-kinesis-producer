package com.amazonaws.services.kinesis.producer;

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