--
-- Copyright (C) 2009-2012 Chris McClelland
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
--
library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity top_level is
	generic (
		NUM_DEVS     : integer := 1
	);
	port(
		-- FX2LP interface ---------------------------------------------------------------------------
		fx2Clk_in      : in    std_logic;                    -- 48MHz clock from FX2LP
		fx2Addr_out    : out   std_logic_vector(1 downto 0); -- select FIFO: "00" for EP2OUT, "10" for EP6IN
		fx2Data_io     : inout std_logic_vector(7 downto 0); -- 8-bit data to/from FX2LP

		-- When EP2OUT selected:
		fx2Read_out    : out   std_logic;                    -- asserted (active-low) when reading from FX2LP
		fx2OE_out      : out   std_logic;                    -- asserted (active-low) to tell FX2LP to drive bus
		fx2GotData_in  : in    std_logic;                    -- asserted (active-high) when FX2LP has data for us

		-- When EP6IN selected:
		fx2Write_out   : out   std_logic;                    -- asserted (active-low) when writing to FX2LP
		fx2GotRoom_in  : in    std_logic;                    -- asserted (active-high) when FX2LP has room for more data from us
		fx2PktEnd_out  : out   std_logic;                    -- asserted (active-low) when a host read needs to be committed early

		-- Peripheral interface ----------------------------------------------------------------------
		spiClk_out     : out   std_logic;
		spiData_out    : out   std_logic;
		spiData_in     : in    std_logic;
		spiCS_out      : out   std_logic_vector(NUM_DEVS-1 downto 0)
	);
end entity;

architecture structural of top_level is
	-- Channel read/write interface -----------------------------------------------------------------
	signal chanAddr   : std_logic_vector(6 downto 0);  -- the selected channel (0-127)

	-- Host >> FPGA pipe:
	signal h2fData    : std_logic_vector(7 downto 0);  -- data lines used when the host writes to a channel
	signal h2fValid   : std_logic;                     -- '1' means "on the next clock rising edge, please accept the data on h2fData"
	signal h2fReady   : std_logic;                     -- channel logic can drive this low to say "I'm not ready for more data yet"

	-- Host << FPGA pipe:
	signal f2hData    : std_logic_vector(7 downto 0);  -- data lines used when the host reads from a channel
	signal f2hValid   : std_logic;                     -- channel logic can drive this low to say "I don't have data ready for you"
	signal f2hReady   : std_logic;                     -- '1' means "on the next clock rising edge, put your next byte of data on f2hData"
	-- ----------------------------------------------------------------------------------------------

	-- Needed so that the comm_fpga_fx2 module can drive both fx2Read_out and fx2OE_out
	signal fx2Read    : std_logic;

	-- Reset signal so host can delay startup
	signal fx2Reset   : std_logic;
begin
	-- CommFPGA module
	fx2Read_out <= fx2Read;
	fx2OE_out <= fx2Read;
	fx2Addr_out(0) <=  -- So fx2Addr_out(1)='0' selects EP2OUT, fx2Addr_out(1)='1' selects EP6IN
		'0' when fx2Reset = '0'
		else 'Z';
	comm_fpga_fx2 : entity work.comm_fpga_fx2
		port map(
			clk_in         => fx2Clk_in,
			reset_in       => '0',
			reset_out      => fx2Reset,
			
			-- FX2LP interface
			fx2FifoSel_out => fx2Addr_out(1),
			fx2Data_io     => fx2Data_io,
			fx2Read_out    => fx2Read,
			fx2GotData_in  => fx2GotData_in,
			fx2Write_out   => fx2Write_out,
			fx2GotRoom_in  => fx2GotRoom_in,
			fx2PktEnd_out  => fx2PktEnd_out,

			-- DVR interface -> Connects to application module
			chanAddr_out   => chanAddr,
			h2fData_out    => h2fData,
			h2fValid_out   => h2fValid,
			h2fReady_in    => h2fReady,
			f2hData_in     => f2hData,
			f2hValid_in    => f2hValid,
			f2hReady_out   => f2hReady
		);

	-- Switches & LEDs application
	spi_talk_app : entity work.spi_talk
      generic map (
         NUM_DEVS     => NUM_DEVS
		)
		port map(
			clk_in       => fx2Clk_in,
			
			-- DVR interface -> Connects to comm_fpga module
			chanAddr_in  => chanAddr,
			h2fData_in   => h2fData,
			h2fValid_in  => h2fValid,
			h2fReady_out => h2fReady,
			f2hData_out  => f2hData,
			f2hValid_out => f2hValid,
			f2hReady_in  => f2hReady,
			
			-- Peripheral interface
			spiClk_out   => spiClk_out,
			spiData_out  => spiData_out,
			spiData_in   => spiData_in,
			spiCS_out    => spiCS_out
		);
end architecture;
