package software.amazon.kinesis.producer;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class BinaryToHexConverterTest {

	@Test
	public void testConvert() {
		byte[] bytes = {0, 1, 2, 3, 124, 125, 126, 127};
		assertEquals("000102037C7D7E7F", BinaryToHexConverter.convert(bytes));
	}

}