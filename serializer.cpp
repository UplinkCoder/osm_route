#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FLAG_NO_CRC32 (1 << 2);

static const uint16_t g_flags = 0
#ifdef NO_CRC32
 | FLAG_NO_CRC32
#endif
;

#pragma once

#ifdef TEST_MAIN
#  define HAD_TEST_MAIN_SERIALIZER
#  undef TEST_MAIN
#endif

#ifndef NO_CRC32

#include "crc32.c"

#endif
#ifdef HAD_TEST_MAIN_SERIALIZER
# pragma message("Runninng tests")
# define TEST_MAIN
#endif

struct Serializer
{
    enum class serialize_mode_t { Reading, Writing };

    FILE* fd;
    const char* m_filename;
    serialize_mode_t m_mode;

    uint32_t crc;
    uint32_t invCrc;

    uint64_t bytes_in_file      = 0;
    uint64_t position_in_file   = 0;

    uint32_t position_in_buffer = 0;
    uint32_t buffer_used        = 0;

    static const int FLUSH_GRANULARITY = 4096;
    static const int BUFFER_SIZE       = (FLUSH_GRANULARITY * 2);

    uint8_t buffer[BUFFER_SIZE];

    uint32_t r_invCrc; // reader only

private:
    uint32_t ReadFlush(void);
    uint32_t WriteFlush(void);

public:
    Serializer(const char* filename, serialize_mode_t mode);
    ~Serializer();

    /// returns the current virtual cursor in the file.
    uint32_t CurrentPosition(void);

    /// sets the serializer to a virtual cursor in the file
    /// Returns: the position it came from
    uint32_t SetPosition(uint32_t position);

    /// Returns number of bytes read. 0 means error.
    uint8_t ReadShortUint(uint32_t* value);

    /// Returns number of bytes written. 0 means error.
    uint8_t WriteShortUint(uint32_t value);

    /// Returns number of bytes read. 0 means error.
    uint8_t ReadShortInt(int32_t* value);

    /// Returns number of bytes written. 0 means error.
    uint8_t WriteShortInt(int32_t value);


    /// May not write all the data in one go
    /// use in a loop or via the WRITE_ARRAY_DATA_SIZE macro
    /// Returns the number of bytes written
    uint32_t WriteRawData(const void* data, uint32_t size);

    /// May not read all the data in one go;
    /// use in a loop or via the READ_ARRAY_DATA_SIZE macro
    /// Returns the number of bytes read
    uint32_t ReadRawData(void* data, uint32_t size);

    uint32_t ReadU32(void);
    void WriteU32(uint32_t value);

    void WriteU8(uint8_t);
    uint8_t ReadU8(void);

    void WriteU64(uint64_t value);
    uint64_t ReadU64(void);

    void WriteF64(double);
    double ReadF64(void);
} ;

#define WRITE_ARRAY_DATA(WRITER, ARRAY) \
    WRITE_ARRAY_DATA_SIZE(WRITER, ARRAY, sizeof(ARRAY))

#define READ_ARRAY_DATA(READER, ARRAY) \
    READ_ARRAY_DATA_SIZE(READER, ARRAY, sizeof(ARRAY))

#define WRITE_ARRAY_DATA_SIZE(WRITER, ARRAY, SIZE) \
    { \
        char* p = (char*)(ARRAY); \
        unsigned int bytes_left = (SIZE); \
        unsigned int bytes_written = 0; \
        do \
        { \
            bytes_written = (WRITER).WriteRawData(p, bytes_left); \
            p += bytes_written; \
            bytes_left -= bytes_written; \
        } while (bytes_written); \
        assert(bytes_left == 0); \
    }

#define READ_ARRAY_DATA_SIZE(READER, ARRAY, SIZE) \
    { \
        char* p = (char*)(ARRAY); \
        unsigned int bytes_left = (SIZE); \
        unsigned int bytes_read = 0; \
        do \
        { \
            bytes_read = (READER).ReadRawData(p, bytes_left); \
            p += bytes_read; \
            bytes_left -= bytes_read; \
        } while (bytes_read); \
        assert(bytes_left == 0); \
    }

uint32_t Serializer::CurrentPosition(void)
{
    return position_in_file - (buffer_used - position_in_buffer);
    //(position_in_file - buffer_used) + position_in_buffer;
}

//TODO patching a file using SetPosition invalidates incremental crc
// therefore once it is used we disable incremental crc and do
// a full crc at the end
uint32_t Serializer::SetPosition(uint32_t p) {
    // disable crc
    crc = invCrc = 0;

    const auto oldP = CurrentPosition();
    // flush out the whole buffer if writing

    if (m_mode == serialize_mode_t::Writing) {
        while(WriteFlush()) {}

        assert(position_in_buffer == 0);
        assert(buffer_used == 0);
        // after flushing the whole buffer the cursor should be at the start
        assert(position_in_file == oldP);
        // and the position in the file sould be equal to our previous virtual position
        fseek(fd, p, SEEK_SET);
        position_in_file = p;
    } else {
        assert(!"TODO");
    }

    return oldP;
}

uint32_t Serializer::ReadFlush (void) {
    assert(buffer_used >= position_in_buffer); // general invariant

    const uint64_t bytes_available = bytes_in_file - position_in_file;
    const auto old_bytes_in_buffer = buffer_used - position_in_buffer;

    // assert(bytes_available > 0);

    uint32_t size_to_read = BUFFER_SIZE - buffer_used;
    if (bytes_available < size_to_read)
        size_to_read = bytes_available;

    memmove(buffer, buffer + position_in_buffer, old_bytes_in_buffer);
    auto bytes_read = fread(buffer + old_bytes_in_buffer, 1, size_to_read, fd);
    assert(bytes_read == size_to_read);

#ifndef NO_CRC32
    if (crc != invCrc)
    {
        crc = crc32c(crc, buffer + old_bytes_in_buffer, size_to_read);
        invCrc = ~crc;
    }
#endif

    buffer_used = old_bytes_in_buffer + bytes_read;
    position_in_buffer = 0;

    position_in_file += size_to_read;

    return size_to_read;
}

uint32_t Serializer::WriteFlush (void) {
    assert(m_mode == serialize_mode_t::Writing);

    uint32_t bytes_to_flush = FLUSH_GRANULARITY;
    if (position_in_buffer < bytes_to_flush)
        bytes_to_flush = position_in_buffer;
#ifndef NO_CRC32
    if (crc != invCrc)
    {
        crc = crc32c(crc, buffer, bytes_to_flush);
        invCrc = ~crc;
    }
#endif
    fwrite(buffer, 1, bytes_to_flush, fd);

    position_in_file += bytes_to_flush;
    position_in_buffer -= bytes_to_flush;
    // cpy overhang
    memmove(buffer, buffer + bytes_to_flush, position_in_buffer);

    return bytes_to_flush;
}


Serializer::Serializer(const char* filename, serialize_mode_t mode) :
    m_filename(filename), m_mode(mode), crc(~0)  {
    fd = fopen(filename, m_mode == serialize_mode_t::Writing ? "w+b" : "rb");

    if (!fd)
    {
        perror("Serializer()");
    }
    else
    {
        if (mode == serialize_mode_t::Writing)
        {
            fwrite("OSMb", 4, 1, fd); // write magic number
            uint16_t versionNumber = 1;
            if (ferror(fd))
            {
                perror("Serializer()");
                assert(0);
            }
            fwrite(&versionNumber, sizeof(versionNumber), 2, fd); // write version number

            uint16_t flags = g_flags;
            fwrite(&versionNumber, sizeof(flags), 1, fd); // write version number
            // placeholder for ~crc;
            uint32_t invCrc = ~crc;
            // NOTE: crc is not valid yet we just write it as a place_holder
            fwrite(&crc, sizeof(crc), 1, fd);
            fwrite(&invCrc, sizeof(invCrc), 1, fd);
            assert(ftell(fd) == 16);

            position_in_file = 16;
        }
        else
        {
            fseek(fd, 0, SEEK_END);
            bytes_in_file = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            int bytes_read = 0;

            char magic[4];
            bytes_read += fread(&magic, 1, sizeof(magic), fd);

            uint16_t versionNumber;
            bytes_read += fread(&versionNumber, 1, sizeof(versionNumber), fd);

            uint16_t flags;
            bytes_read += fread(&versionNumber, 1, sizeof(flags), fd);

            uint32_t r_crc;
            bytes_read += fread(&r_crc, 1, sizeof(r_crc), fd);

            bytes_read += fread(&r_invCrc, 1, sizeof(r_invCrc), fd);
            if(r_crc != ~r_invCrc)
            {
                printf("read crc %x and invCrc %x \n", r_crc, r_invCrc);
                fprintf(stderr, "initial CRC check failed ... file '%s' is corrupted\n", m_filename);
                abort();
            }

            position_in_file = bytes_read;
            assert(ftell(fd) == 16 && position_in_file == 16);
            assert(0 == memcmp(&magic, "OSMb", 4));
        }
    }
}

Serializer::~Serializer() {
    // TODO maybe pad the file to a multiple of 4?
    if (m_mode == serialize_mode_t::Writing)
    {
        WriteFlush();

#ifndef NO_CRC32
        if (crc == invCrc)
        {
            crc = ~0;
            const auto file_size = position_in_file;

            // incremental crc was disabled
            // we have to do the whole thing now
            if (fseek(fd, 16, SEEK_SET))
                if (ferror(fd))
                    perror("seeking");

            position_in_file = 16;
            while(position_in_file < file_size)
            {
                const auto bytes_read =
                    fread(buffer, 1, BUFFER_SIZE, fd);
                crc = crc32c(crc, buffer, bytes_read);
                position_in_file += bytes_read;
                if(!bytes_read)
                {
                    if (ferror(fd))
                        perror("crc_recalc ");
                    break;
                }
            }
        }

        {
            fseek(fd, 8, SEEK_SET);
            fwrite(&crc, 1, sizeof(crc), fd);
            uint32_t invCrc_ = ~crc;
            fwrite(&invCrc_, 1, sizeof(invCrc_), fd);
        }
#endif
    }
    if (m_mode == serialize_mode_t::Reading)
    {
        assert(position_in_file == bytes_in_file);
#ifndef NO_CRC32
        if (crc == invCrc)
        {
            //TODO recalc crc
        }
        else
        {
            assert(~crc == r_invCrc);
        }
#endif
    }
    fclose(fd);
}

uint8_t Serializer::ReadShortUint(uint32_t* ptr) {
    assert(position_in_buffer <= buffer_used);

    if ((buffer_used - position_in_buffer) < 4
        && bytes_in_file - position_in_file > 0)
    {
        ReadFlush();
    }

    assert(buffer_used >= 1);

    auto mem = buffer + position_in_buffer;
    const auto first_byte = *mem++;
    position_in_buffer++;

    uint32_t value = (first_byte & 0x7f);
    const auto has_next = (first_byte & 0x80) != 0;
    if (has_next)
    {
        const auto second_byte = *mem++;
        position_in_buffer++;

        value |= ((second_byte & 0x7f) << 7);
        const auto has_next2 = (second_byte & 0x80) != 0;

        // read next byte
        if (has_next2)
        {
            value |= ((*(uint16_t*)mem) << 14);
            position_in_buffer += 2;
        }
    }

    *ptr = value;
    return (uint8_t)(buffer - mem);
}

uint8_t Serializer::WriteShortUint(uint32_t value) {
    assert(m_mode == serialize_mode_t::Writing);

    if(value >= 0x40000000)
        return 0;

    if (position_in_buffer >= FLUSH_GRANULARITY)
    {
        // try to flush in 4092 chunks
        WriteFlush();
    }
    if (value < 0x80)
    {
        buffer[position_in_buffer++] = (uint8_t)value;
        return 1;
    }
    else if (value < (1 << 14))
    {
        buffer[position_in_buffer++] = (uint8_t)(value | 0x80);
        buffer[position_in_buffer++] = (uint8_t)(value >>  7);
        return 2;
    }
    else
    {
        buffer[position_in_buffer++] = (uint8_t)((value >> 0) | 0x80);
        buffer[position_in_buffer++] = (uint8_t)((value >> 7) | 0x80);
        buffer[position_in_buffer++] = (uint8_t)(value >> 14);
        buffer[position_in_buffer++] = (uint8_t)(value >> 22);
        return 4;
    }
}

#define ABS(VALUE) \
    (((VALUE) > (decltype(VALUE))0L) ? (VALUE) : (~(VALUE)) + 1)

inline bool FitsInShortInt(int64_t value)
{
    return (ABS(value) < (1 << 29));
}

uint8_t Serializer::WriteShortInt(int32_t value) {
    assert(m_mode == serialize_mode_t::Writing);
    uint32_t abs_value = ABS(value);

    if (abs_value & 0x60000000)
    {
        return 0;
    }
    uint8_t result;
    bool isNegative = ((value & (1 << 31)) != 0);
    // printf("isNegative: %d\n", isNegative);
    uint32_t transformed_value = (abs_value << 1) | isNegative;

    if (position_in_buffer >= FLUSH_GRANULARITY)
    {
        // try to flush in 4092 chunks
        WriteFlush();
    }

    if (abs_value < (1 << 6))
    {
        buffer[position_in_buffer++] = (uint8_t)transformed_value;
        result = 1;
    }
    else if (abs_value < (1 << 13))
    {
        buffer[position_in_buffer++] = (uint8_t)(transformed_value | 0x80);
        buffer[position_in_buffer++] = (uint8_t)(transformed_value >> 7);
        result = 2;
    }
    else
    {
        buffer[position_in_buffer++] = (uint8_t)((transformed_value >> 0) | 0x80);
        buffer[position_in_buffer++] = (uint8_t)((transformed_value >> 7) | 0x80);
        buffer[position_in_buffer++] = (uint8_t)(transformed_value >> 14);
        buffer[position_in_buffer++] = (uint8_t)(transformed_value >> 22);
        result = 4;
    }

    return result;
}

uint8_t Serializer::ReadShortInt(int32_t* ptr) {
    assert(position_in_buffer <= buffer_used);

    if ((buffer_used - position_in_buffer) < 4
        && bytes_in_file - position_in_file > 0)
    {
        ReadFlush();
    }

    const auto old_position_in_buffer = position_in_buffer;

    assert(buffer_used >= 1);

    auto mem = buffer + position_in_buffer;
    const auto first_byte = *mem++;
    position_in_buffer++;

    bool isNegative = first_byte & 1;
    uint32_t value = ((first_byte & 0x7f) >> 1);

    const auto has_next = (first_byte & 0x80) != 0;
    if (has_next)
    {
        const auto second_byte = *mem++;
        position_in_buffer++;

        value |= ((second_byte & 0x7f) << 6);
        const auto has_next2 = (second_byte & 0x80) != 0;

        // read next byte
        if (has_next2)
        {
            value |= ((*(uint16_t*)mem) << 13);
            position_in_buffer += 2;
        }
    }

    if (isNegative)
        value = ~value + 1;

    *ptr = (int32_t)value;

    return (uint8_t)(position_in_buffer - old_position_in_buffer);
}
#undef ABS

uint32_t Serializer::WriteRawData(const void* data, uint32_t size) {
    assert(m_mode == serialize_mode_t::Writing);

    if (position_in_buffer > FLUSH_GRANULARITY)
        WriteFlush();

    if (size > FLUSH_GRANULARITY)
        size = FLUSH_GRANULARITY;

    memcpy(buffer + position_in_buffer, data, size);
    position_in_buffer += size;
    assert(position_in_buffer < BUFFER_SIZE);

    return size;
}

uint32_t Serializer::ReadRawData(void* data, uint32_t size) {
    assert(m_mode == serialize_mode_t::Reading);

    if ((buffer_used - position_in_buffer) < FLUSH_GRANULARITY)
        ReadFlush();

    uint32_t bytes_avialable = (buffer_used - position_in_buffer);

    if (size > FLUSH_GRANULARITY)
        size = FLUSH_GRANULARITY;

    if (size > bytes_avialable)
        size = bytes_avialable;

    // printf("position in buffer");
    memcpy(data, buffer + position_in_buffer, size);
    position_in_buffer += size;

    assert(position_in_buffer < BUFFER_SIZE);

    return size;
}

uint32_t Serializer::ReadU32(void) {
    assert(m_mode == serialize_mode_t::Reading);

    if (buffer_used - position_in_buffer < 4)
    {
        ReadFlush();
    }
    assert(buffer_used - position_in_buffer >= 4);
    uint32_t result =  (*(uint32_t*)(buffer + position_in_buffer));
    position_in_buffer += 4;
    return result;
}

void Serializer::WriteU32(uint32_t value) {
    assert(m_mode == serialize_mode_t::Writing);

   if (position_in_buffer >= FLUSH_GRANULARITY)
   {
       WriteFlush();
   }

    (*(uint32_t*)(buffer + position_in_buffer)) = value;
    position_in_buffer += 4;
}

uint8_t Serializer::ReadU8(void) {
    assert(m_mode == serialize_mode_t::Reading);

    if (buffer_used - position_in_buffer < 1)
    {
        ReadFlush();
    }
    assert(buffer_used - position_in_buffer >= 1);
    uint8_t result =  (*(uint8_t*)(buffer + position_in_buffer));
    position_in_buffer += 1;
    return result;
}

void Serializer::WriteU8(uint8_t value) {
    assert(m_mode == serialize_mode_t::Writing);

   if (position_in_buffer >= FLUSH_GRANULARITY)
   {
       WriteFlush();
   }

    (*(uint8_t*)(buffer + position_in_buffer)) = value;
    position_in_buffer += 1;
}

uint64_t Serializer::ReadU64(void) {
    assert(m_mode == serialize_mode_t::Reading);

    if ((buffer_used - position_in_buffer) < 8)
    {
        ReadFlush();
    }

    assert(buffer_used - position_in_buffer >= 8);
    uint64_t result =  (*(uint64_t*)(buffer + position_in_buffer));
    position_in_buffer += sizeof(result);
    return result;
}

void Serializer::WriteU64(uint64_t value) {
    assert(m_mode == serialize_mode_t::Writing);

   if (position_in_buffer >= FLUSH_GRANULARITY)
   {
       WriteFlush();
   }

    (*(uint64_t*)(buffer + position_in_buffer)) = value;
    position_in_buffer += sizeof(value);
}

double Serializer::ReadF64(void) {
    assert(m_mode == serialize_mode_t::Reading);

    if (buffer_used - position_in_buffer < sizeof(double))
    {
        ReadFlush();
    }
    assert(buffer_used - position_in_buffer >= sizeof(double));
    double result =  (*(double*)(buffer + position_in_buffer));
    position_in_buffer += sizeof(double);
    return result;
}

void Serializer::WriteF64(double value) {
    assert(m_mode == serialize_mode_t::Writing);

   if (position_in_buffer >= FLUSH_GRANULARITY)
   {
       WriteFlush();
   }

    (*(decltype(value)*)(buffer + position_in_buffer)) = value;
    position_in_buffer += sizeof(value);
}

#ifdef TEST_MAIN


static void test_serializer(void) {
    using serialize_mode_t = Serializer::serialize_mode_t;

    {
        Serializer writer { "test_s.dat", serialize_mode_t::Writing };

        auto CurrentPosition = [&writer] (void) {
            return writer.CurrentPosition();
        };
        writer.WriteU32(19);
        assert(CurrentPosition() == 20);
        writer.WriteShortUint(29);
        assert(CurrentPosition() == 21);
        const auto second_short_pos = CurrentPosition();
        writer.WriteShortUint(227);
        assert(CurrentPosition() == 23);
        uint8_t x[Serializer::BUFFER_SIZE * 2];
        for(int i = 0;
            i < Serializer::BUFFER_SIZE * 2;
            i++
        )
        {
            x[i] = (uint8_t)(i + 1);
        }

        WRITE_ARRAY_DATA(writer, x);
        assert(CurrentPosition() == 23 + sizeof(x));

        const auto oldP = writer.SetPosition(second_short_pos);
        writer.WriteShortUint(300);
        writer.SetPosition(oldP);

        writer.WriteU32(1993 << 13);

        writer.WriteU8(0);

        for(int32_t v = -(1 << 3);
            v < (-(1 << 3)) + 128;
            v++)
        {
            writer.WriteShortInt(v);
        }

        for(int32_t v = -(1 << 11);
            v < (-(1 << 3)) + 128;
            v++)
        {
            writer.WriteShortInt(v);
        }

        for(int32_t v = -(1 << 14);
            v < (-(1 << 14)) + 128;
            v++)
        {
            writer.WriteShortInt(v);
        }

        for(int32_t v = (1 << 14);
            v < ((1 << 14)) + 128;
            v++)
        {
            writer.WriteShortInt(v);
        }
    }
    {
        Serializer reader { "test_s.dat", serialize_mode_t::Reading };

        auto CurrentPosition = [&reader] (void) {
            return reader.CurrentPosition();
        };

        assert(reader.ReadU32() == 19);
        assert(CurrentPosition() == 20);
        uint32_t result;
        reader.ReadShortUint(&result);
        assert(CurrentPosition() == 21);
        assert(result == 29);

        reader.ReadShortUint(&result);
        assert(result == 300);
        assert(CurrentPosition() == 23);
        uint8_t x[Serializer::BUFFER_SIZE * 2];

        READ_ARRAY_DATA(reader, x);
        assert(CurrentPosition() == 23 + sizeof(x));

        result = reader.ReadU32();
        for(int i = 0;
            i < Serializer::BUFFER_SIZE * 2;
            i++
        )
        {
            assert(x[i] == (uint8_t)(i + 1));
        }
        assert(result == 1993 << 13);

        int32_t should_be_zero;
        reader.ReadShortInt(&should_be_zero);
        assert(should_be_zero == 0);

        for (int32_t v = -(1 << 3);
            v < (-(1 << 3)) + 128;
            v++)
        {
            int32_t read_value;
            reader.ReadShortInt(&read_value);
            assert (read_value == v);
        }


        for (int32_t v = -(1 << 11);
            v < (-(1 << 3)) + 128;
            v++)
        {
            int32_t read_value;
            reader.ReadShortInt(&read_value);
            assert (read_value == v);
        }

        for (int32_t v = -(1 << 14);
            v < (-(1 << 14)) + 128;
            v++)
        {
            int32_t read_value;
            reader.ReadShortInt(&read_value);
            assert (read_value == v);
        }

        for (int32_t v = (1 << 14);
            v < ((1 << 14)) + 128;
            v++)
        {
            int32_t read_value;
            reader.ReadShortInt(&read_value);
            assert (read_value == v);
        }
    }
}

#ifdef __cplusplus
  extern "C" int puts(const char* s);
#else
  extern int puts(const char* s);
#endif

int main(int argc, char* argv[])
{
    test_serializer();

    puts("test succseeded");
}
#endif
