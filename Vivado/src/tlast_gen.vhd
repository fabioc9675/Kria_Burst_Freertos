library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity axis_packet_controller is
    Generic (
        MAX_SAMPLES : integer := 524288
    );
    Port (
        clk             : in  STD_LOGIC;
        resetn          : in  STD_LOGIC;
        
        -- Interfaz de entrada (Viene de tu DDS/Lógica)
        s_axis_tdata    : in  STD_LOGIC_VECTOR(15 downto 0);
        s_axis_tvalid   : in  STD_LOGIC;
        s_axis_tready   : out STD_LOGIC;
        
        -- Interfaz de salida (Va al AXI DMA S2MM)
        m_axis_tdata    : out STD_LOGIC_VECTOR(31 downto 0);
        m_axis_tvalid   : out STD_LOGIC;
        m_axis_tready   : in  STD_LOGIC;
        m_axis_tlast    : out STD_LOGIC
    );
end axis_packet_controller;

architecture Behavioral of axis_packet_controller is
    signal sample_count : integer range 0 to MAX_SAMPLES := 0;
    signal can_transfer : STD_LOGIC;
    -- Nueva señal para controlar cuándo empezar un paquete
    signal is_sending   : STD_LOGIC := '0'; 
begin

    -- El apretón de manos solo es válido si estamos en medio de un paquete activo
    can_transfer <= s_axis_tvalid and m_axis_tready and is_sending;

    m_axis_tdata  <= X"0000" & s_axis_tdata;
    m_axis_tvalid <= s_axis_tvalid and is_sending;
    
    -- Solo aceptamos datos de la entrada si el DMA está listo y queremos enviar
    s_axis_tready <= m_axis_tready and is_sending;

    -- TLAST sincronizado
    m_axis_tlast <= '1' when (sample_count = MAX_SAMPLES - 1 and can_transfer = '1') else '0';

    process(clk)
    begin
        if rising_edge(clk) then
            if resetn = '0' then
                sample_count <= 0;
                is_sending <= '0';
            else
                -- LÓGICA DE CONTROL:
                -- Si el DMA está listo (tready='1') y no estamos enviando, 
                -- significa que el PS acaba de pedir un paquete nuevo.
                if is_sending = '0' and m_axis_tready = '1' then
                    is_sending   <= '1';
                    sample_count <= 0; -- Empezamos SIEMPRE desde la muestra 0
                
                -- Si estamos enviando y ocurre un handshake
                elsif can_transfer = '1' then
                    if sample_count = (MAX_SAMPLES - 1) then
                        sample_count <= 0;
                        is_sending   <= '0'; -- Cerramos el paquete y esperamos al próximo tready
                    else
                        sample_count <= sample_count + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;
end Behavioral;