//! Protocol Buffers, as much as Steam's broadcast messages use: varints, 32- and 64-bit fixed
//! fields and length-delimited fields. A message is read into its fields in order and written field
//! by field, so a message from Steam can be copied with a few fields changed.

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Value<'a> {
    Varint(u64),
    Fixed64(u64),
    Bytes(&'a [u8]),
    Fixed32(u32),
}

/// A message's fields, in the order they came; None if the bytes aren't a message.
pub fn fields(mut data: &[u8]) -> Option<Vec<(u32, Value<'_>)>> {
    let mut out = Vec::new();
    while !data.is_empty() {
        let key = read_varint(&mut data)?;
        let number = (key >> 3) as u32;
        let value = match key & 7 {
            0 => Value::Varint(read_varint(&mut data)?),
            1 => {
                let bytes: [u8; 8] = data.get(..8)?.try_into().ok()?;
                data = &data[8..];
                Value::Fixed64(u64::from_le_bytes(bytes))
            }
            2 => {
                let length = read_varint(&mut data)? as usize;
                let bytes = data.get(..length)?;
                data = &data[length..];
                Value::Bytes(bytes)
            }
            5 => {
                let bytes: [u8; 4] = data.get(..4)?.try_into().ok()?;
                data = &data[4..];
                Value::Fixed32(u32::from_le_bytes(bytes))
            }
            _ => return None,
        };
        out.push((number, value));
    }
    Some(out)
}

fn read_varint(data: &mut &[u8]) -> Option<u64> {
    let mut value = 0u64;
    for shift in (0..64).step_by(7) {
        let (&byte, rest) = data.split_first()?;
        *data = rest;
        value |= u64::from(byte & 0x7f) << shift;
        if byte & 0x80 == 0 {
            return Some(value);
        }
    }
    None
}

/// The fields of a message, looked up by number (the first of a repeated one).
pub struct Message<'a> {
    pub fields: Vec<(u32, Value<'a>)>,
}

impl<'a> Message<'a> {
    pub fn parse(data: &'a [u8]) -> Option<Self> {
        fields(data).map(|fields| Self { fields })
    }

    fn get(&self, number: u32) -> Option<Value<'a>> {
        self.fields.iter().find(|(n, _)| *n == number).map(|(_, v)| *v)
    }

    pub fn varint(&self, number: u32) -> Option<u64> {
        match self.get(number)? {
            Value::Varint(v) => Some(v),
            _ => None,
        }
    }

    pub fn fixed64(&self, number: u32) -> Option<u64> {
        match self.get(number)? {
            Value::Fixed64(v) => Some(v),
            _ => None,
        }
    }

    pub fn bytes(&self, number: u32) -> Option<&'a [u8]> {
        match self.get(number)? {
            Value::Bytes(v) => Some(v),
            _ => None,
        }
    }

    pub fn string(&self, number: u32) -> Option<String> {
        self.bytes(number).map(|b| String::from_utf8_lossy(b).into_owned())
    }
}

#[derive(Default)]
pub struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    pub fn new() -> Self {
        Self::default()
    }

    fn varint_raw(&mut self, mut value: u64) {
        loop {
            let byte = (value & 0x7f) as u8;
            value >>= 7;
            if value == 0 {
                self.bytes.push(byte);
                return;
            }
            self.bytes.push(byte | 0x80);
        }
    }

    fn key(&mut self, number: u32, wire: u64) {
        self.varint_raw(u64::from(number) << 3 | wire);
    }

    pub fn value(&mut self, number: u32, value: Value) -> &mut Self {
        match value {
            Value::Varint(v) => self.varint(number, v),
            Value::Fixed64(v) => self.fixed64(number, v),
            Value::Bytes(v) => self.bytes(number, v),
            Value::Fixed32(v) => {
                self.key(number, 5);
                self.bytes.extend_from_slice(&v.to_le_bytes());
                self
            }
        }
    }

    pub fn varint(&mut self, number: u32, value: u64) -> &mut Self {
        self.key(number, 0);
        self.varint_raw(value);
        self
    }

    pub fn fixed64(&mut self, number: u32, value: u64) -> &mut Self {
        self.key(number, 1);
        self.bytes.extend_from_slice(&value.to_le_bytes());
        self
    }

    pub fn bytes(&mut self, number: u32, value: &[u8]) -> &mut Self {
        self.key(number, 2);
        self.varint_raw(value.len() as u64);
        self.bytes.extend_from_slice(value);
        self
    }

    pub fn string(&mut self, number: u32, value: &str) -> &mut Self {
        self.bytes(number, value.as_bytes())
    }

    pub fn finish(&mut self) -> Vec<u8> {
        std::mem::take(&mut self.bytes)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_every_kind() {
        let bytes = Writer::new()
            .varint(1, 300)
            .fixed64(2, 0x0102_0304_0506_0708)
            .string(4, "Mac")
            .value(7, Value::Fixed32(9))
            .varint(26, 1)
            .finish();
        let message = Message::parse(&bytes).unwrap();
        assert_eq!(message.varint(1), Some(300));
        assert_eq!(message.fixed64(2), Some(0x0102_0304_0506_0708));
        assert_eq!(message.string(4).as_deref(), Some("Mac"));
        assert_eq!(message.varint(26), Some(1));
        assert_eq!(message.fields[3], (7, Value::Fixed32(9)));
        // Copying every field reproduces the message.
        let mut copy = Writer::new();
        for (number, value) in &message.fields {
            copy.value(*number, *value);
        }
        assert_eq!(copy.finish(), bytes);
    }

    #[test]
    fn known_encoding() {
        // The discovery probe body Steam Link sends: seq_num = 1.
        assert_eq!(Writer::new().varint(1, 1).finish(), [0x08, 0x01]);
        assert!(Message::parse(&[0x08]).is_none());
    }
}
