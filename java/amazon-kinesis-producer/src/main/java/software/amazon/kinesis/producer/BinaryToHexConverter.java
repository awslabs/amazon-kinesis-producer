package software.amazon.kinesis.producer;

public class BinaryToHexConverter {

	private static final BinaryToHexConverter INSTANCE = new BinaryToHexConverter();
	private static final char[] HEX_CODE = "0123456789ABCDEF".toCharArray();

	/**
	 * Converts an array of bytes into a hex string.
	 *
	 * @param data An array of bytes
	 * @return A string containing a lexical representation of xsd:hexBinary
	 * @throws IllegalArgumentException if {@code data} is null.
	 */
	public static String convert(byte[] data) {
		return INSTANCE.convertToHex(data);
	}

	private String convertToHex(byte[] data) {
		StringBuilder r = new StringBuilder(data.length * 2);
		for (byte b : data) {
			r.append(HEX_CODE[(b >> 4) & 0xF]);
			r.append(HEX_CODE[(b & 0xF)]);
		}
		return r.toString();
	}

}
